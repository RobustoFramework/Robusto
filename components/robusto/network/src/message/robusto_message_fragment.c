/**
 * @file robusto_message_fragment.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto message fragmentation and assembly (and resending)
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robusto_message.h>
#include <robusto_network_init.h>
#include <robusto_logging.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include <robusto_states.h>
#include <robusto_time.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
#include <robusto_halow.h>
#include <robusto_concurrency.h>
#include <stdlib.h>
#endif
#ifdef USE_ESPIDF
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#else
#include <robusto_sys_queue.h>
#endif
#include <string.h>

// Define a short cut. CRC + CONTEXT_BYTE + FRAG_TYPE + Fragment counter  the fragment counter uint32_t bytes.
#define FRAG_HEADER_LEN (ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN + 1 + 4)

#if defined(CONFIG_ROBUSTO_TESTING_SKIP_NTH_FRAGMENT) && (CONFIG_ROBUSTO_TESTING_SKIP_NTH_FRAGMENT > -1)
#define SKIP_FRAGMENT_TEST curr_fragment != CONFIG_ROBUSTO_TESTING_SKIP_NTH_FRAGMENT || frag_msg->state == ROB_ST_RETRYING
#define SKIP_FRAGMENT_INDEX CONFIG_ROBUSTO_TESTING_SKIP_NTH_FRAGMENT
#else
#define SKIP_FRAGMENT_TEST 1
#define SKIP_FRAGMENT_INDEX -1
#endif

#define FRAG_RETRY_TOTAL_WAIT_MS 1000
#define FRAG_RUNNING_WAIT_MS 500
#define FRAG_RESULT_WAIT_MS 250
#define FRAG_STATUS_WAIT_MS 250
#define FRAG_SEND_TOTAL_WAIT_MS 30000
#define FRAG_NO_REQUESTED_FRAGMENT UINT32_MAX
#define FRAG_COMPLETION_CACHE_SIZE 8
#define FRAG_COMPLETION_CACHE_TTL_MS ((FRAG_RESULT_WAIT_MS + FRAG_STATUS_WAIT_MS) * 4U)
#define HALOW_FRAGMENT_V2 0x80U
#define HALOW_FRAGMENT_EXTENSION 12U
static char *fragmentation_log_prefix = "NOT SET";

static uint64_t fragment_read_id(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)data[i] << (8U * i);
    return value;
}

static uint32_t fragment_read_round(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static rob_ret_val_t send_fragment_wire(robusto_peer_t *peer, e_media_type media_type,
                                        uint64_t transfer_id, uint32_t round,
                                        uint8_t *data, uint32_t length, bool receipt,
                                        cb_send_message *send_message)
{
    if (media_type != robusto_mt_halow) return send_message(peer, data, length, receipt);
    uint8_t *wire = robusto_malloc(length + HALOW_FRAGMENT_EXTENSION);
    if (wire == NULL) {
        ROB_LOGE(fragmentation_log_prefix, "HaLow v2 packet allocation failed");
        return ROB_ERR_OUT_OF_MEMORY;
    }
    memcpy(wire, data, 6);
    wire[5] |= HALOW_FRAGMENT_V2;
    for (unsigned i = 0; i < 8; ++i) wire[6 + i] = (uint8_t)(transfer_id >> (8U * i));
    for (unsigned i = 0; i < 4; ++i) wire[14 + i] = (uint8_t)(round >> (8U * i));
    memcpy(wire + 18, data + 6, length - 6);
    rob_ret_val_t result = send_message(peer, wire, length + HALOW_FRAGMENT_EXTENSION, receipt);
    robusto_free(wire);
    return result;
}

static void send_wait_yield(e_media_type media_type)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) robusto_halow_service_small_queue();
#endif
    (void)media_type;
    robusto_yield();
}

#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
static uint32_t halow_transfer_nonce;
static uint32_t halow_transfer_counter;
/* One outgoing HaLow worker. RX owns no references to its transfer state. */
static mutex_ref_t halow_feedback_mutex;
static struct {
    robusto_peer_t *peer;
    uint32_t hash;
    uint64_t transfer_id;
    uint32_t round;
    bool round_open;
    uint32_t count;
    uint8_t *map;
    bool resend;
    bool result_pending;
    int16_t result;
} halow_feedback;

static struct {
    robusto_peer_t *peer;
    uint32_t hash;
    uint64_t transfer_id;
    uint32_t round;
    int16_t result;
    uint32_t completed_at;
} halow_completed_feedback[FRAG_COMPLETION_CACHE_SIZE];
static unsigned halow_completed_feedback_next;

static bool halow_feedback_lock(void)
{
    if (halow_feedback_mutex == NULL ||
        robusto_mutex_take(halow_feedback_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        ROB_LOGE(fragmentation_log_prefix, "HaLow fragment feedback mutex unavailable");
        return false;
    }
    return true;
}

static void halow_feedback_unlock(void)
{
    if (robusto_mutex_give(halow_feedback_mutex) != ROB_OK) {
        ROB_LOGE(fragmentation_log_prefix, "HaLow fragment feedback mutex release failed");
        abort();
    }
}

static rob_ret_val_t halow_feedback_begin(fragmented_message_t *message)
{
    uint8_t *map = robusto_malloc(message->fragment_count);
    if (map == NULL) {
        ROB_LOGE(fragmentation_log_prefix, "HaLow fragment feedback map allocation failed");
        return ROB_ERR_OUT_OF_MEMORY;
    }
    if (!halow_feedback_lock()) {
        robusto_free(map);
        return ROB_ERR_MUTEX;
    }
    if (halow_feedback.peer != NULL) {
        halow_feedback_unlock();
        robusto_free(map);
        ROB_LOGE(fragmentation_log_prefix, "Concurrent outgoing HaLow fragment transfer");
        return ROB_ERR_QUEUE_FULL;
    }
    halow_feedback.peer = message->peer;
    halow_feedback.hash = message->hash;
    halow_feedback.transfer_id = message->transfer_id;
    halow_feedback.count = message->fragment_count;
    halow_feedback.map = map;
    halow_feedback.resend = false;
    halow_feedback.result_pending = false;
    halow_feedback_unlock();
    return ROB_OK;
}

static void halow_feedback_end(void)
{
    if (!halow_feedback_lock()) abort();
    uint8_t *map = halow_feedback.map;
    memset(&halow_feedback, 0, sizeof(halow_feedback));
    halow_feedback_unlock();
    robusto_free(map);
}

static void halow_record_feedback(robusto_peer_t *peer, const uint8_t *data, int length, bool resend,
                                  uint64_t transfer_id, uint32_t round)
{
    uint32_t hash;
    memcpy(&hash, data, sizeof(hash));
    if (!halow_feedback_lock()) return;
    if (halow_feedback.peer == NULL || halow_feedback.peer != peer ||
        halow_feedback.hash != hash || halow_feedback.transfer_id != transfer_id) {
        if (!resend && length == 8) {
            int16_t result;
            memcpy(&result, data + ROBUSTO_CRC_LENGTH + 2, sizeof(result));
            for (unsigned i = 0; i < FRAG_COMPLETION_CACHE_SIZE; ++i) {
                if (halow_completed_feedback[i].peer == peer &&
                    halow_completed_feedback[i].hash == hash &&
                    halow_completed_feedback[i].transfer_id == transfer_id &&
                    halow_completed_feedback[i].result == result &&
                    round <= halow_completed_feedback[i].round &&
                    (uint32_t)(r_millis() - halow_completed_feedback[i].completed_at) <=
                        FRAG_COMPLETION_CACHE_TTL_MS) {
                    halow_feedback_unlock();
                    ROB_LOGD(fragmentation_log_prefix,
                             "Duplicate completed HaLow fragment result hash=%lu id=%016llx round=%lu",
                             (unsigned long)hash, (unsigned long long)transfer_id,
                             (unsigned long)round);
                    return;
                }
            }
        }
        halow_feedback_unlock();
        ROB_LOGW(fragmentation_log_prefix, "Ignoring late/unmatched HaLow fragment feedback hash=%lu id=%016llx round=%lu",
                 (unsigned long)hash, (unsigned long long)transfer_id, (unsigned long)round);
        return;
    }
    if (resend) {
        if (length < ROBUSTO_CRC_LENGTH + 2 ||
            (uint32_t)(length - (ROBUSTO_CRC_LENGTH + 2)) != halow_feedback.count) {
            halow_feedback_unlock();
            ROB_LOGE(fragmentation_log_prefix, "Wrong HaLow resend map length: %i", length);
            return;
        }
        if (!halow_feedback.round_open || round != halow_feedback.round) {
            halow_feedback_unlock();
            ROB_LOGD(fragmentation_log_prefix, "Ignoring duplicate/stale HaLow recovery round");
            return;
        }
        for (uint32_t i = 0; i < halow_feedback.count; ++i) {
            if (data[6 + i] > 1U) {
                halow_feedback_unlock();
                ROB_LOGE(fragmentation_log_prefix, "Invalid HaLow receipt bitmap value");
                return;
            }
        }
        memcpy(halow_feedback.map, data + 6, halow_feedback.count);
        halow_feedback.round_open = false;
        halow_feedback.resend = true;
    } else {
        if (length != 8 || round > halow_feedback.round) {
            halow_feedback_unlock();
            ROB_LOGE(fragmentation_log_prefix, "Invalid HaLow completion length/round");
            return;
        }
        memcpy(&halow_feedback.result, data + ROBUSTO_CRC_LENGTH + 2, sizeof(int16_t));
        halow_feedback.result_pending = true;
    }
    halow_feedback_unlock();
}
#endif

SLIST_HEAD(slist_fragmented_messages_head, fragmented_message);

struct slist_fragmented_messages_head fragmented_messages_head;

// Cache the last frag_msg for optimization
static fragmented_message_t *last_frag_msg = NULL;
static robusto_stats_level_t fragment_stats_level = ROBUSTO_STATS_LEVEL_VERBOSE;
static robusto_fragment_stats_t fragment_stats = {0};
static robusto_fragment_stats_t fragment_stats_last_read = {0};

typedef struct completed_fragment_transfer
{
    robusto_peer_t *peer;
    e_media_type media_type;
    uint32_t hash;
    uint64_t transfer_id;
    rob_ret_val_t result;
    uint32_t completed_at;
    bool valid;
} completed_fragment_transfer_t;

static completed_fragment_transfer_t completed_fragment_transfers[FRAG_COMPLETION_CACHE_SIZE];

static uint32_t fragment_first_missing(const fragmented_message_t *frag_msg)
{
    if (frag_msg == NULL || frag_msg->received_fragments == NULL)
    {
        return FRAG_NO_REQUESTED_FRAGMENT;
    }

    for (uint32_t index = 0; index < frag_msg->fragment_count; ++index)
    {
        if (frag_msg->received_fragments[index] == 0)
        {
            return index;
        }
    }

    return FRAG_NO_REQUESTED_FRAGMENT;
}

static uint32_t fragment_missing_count(const fragmented_message_t *frag_msg)
{
    uint32_t missing = 0;

    if (frag_msg == NULL || frag_msg->received_fragments == NULL)
    {
        return 0;
    }

    for (uint32_t index = 0; index < frag_msg->fragment_count; ++index)
    {
        if (frag_msg->received_fragments[index] == 0)
        {
            ++missing;
        }
    }

    return missing;
}

static void fragment_stats_add(uint32_t *counter, uint32_t amount, robusto_stats_level_t required_level)
{
    if (fragment_stats_level >= required_level)
    {
        *counter += amount;
    }
}

static void fragment_stats_copy_delta_field(uint32_t *delta_field, uint32_t current_field, uint32_t *last_read_field)
{
    *delta_field = current_field - *last_read_field;
    *last_read_field = current_field;
}

#define FRAG_STATS_DELTA_FIELD(field_name) \
    fragment_stats_copy_delta_field(&delta->field_name, fragment_stats.field_name, &fragment_stats_last_read.field_name)

void robusto_fragment_stats_set_level(robusto_stats_level_t level)
{
    if (level > ROBUSTO_STATS_LEVEL_VERBOSE)
    {
        level = ROBUSTO_STATS_LEVEL_VERBOSE;
    }
    fragment_stats_level = level;
}

robusto_stats_level_t robusto_fragment_stats_get_level(void)
{
    return fragment_stats_level;
}

void robusto_fragment_stats_get(robusto_fragment_stats_t *total, robusto_fragment_stats_t *delta_since_last_read)
{
    if (total != NULL)
    {
        *total = fragment_stats;
    }
    if (delta_since_last_read != NULL)
    {
        robusto_fragment_stats_t *delta = delta_since_last_read;
        memset(delta, 0, sizeof(*delta));
        FRAG_STATS_DELTA_FIELD(send_started);
        FRAG_STATS_DELTA_FIELD(send_succeeded);
        FRAG_STATS_DELTA_FIELD(send_failed);
        FRAG_STATS_DELTA_FIELD(send_timed_out);
        FRAG_STATS_DELTA_FIELD(request_received);
        FRAG_STATS_DELTA_FIELD(message_received);
        FRAG_STATS_DELTA_FIELD(resend_request_sent);
        FRAG_STATS_DELTA_FIELD(resend_request_received);
        FRAG_STATS_DELTA_FIELD(check_sent);
        FRAG_STATS_DELTA_FIELD(check_received);
        FRAG_STATS_DELTA_FIELD(result_ok_received);
        FRAG_STATS_DELTA_FIELD(result_fail_received);
        FRAG_STATS_DELTA_FIELD(missing_fragments_reported);
        FRAG_STATS_DELTA_FIELD(invalid_fragment_reference);
        FRAG_STATS_DELTA_FIELD(invalid_fragment_index);
        FRAG_STATS_DELTA_FIELD(wrong_fragment_length);
        FRAG_STATS_DELTA_FIELD(wrong_resend_map_length);
        FRAG_STATS_DELTA_FIELD(full_message_crc_mismatch);
        FRAG_STATS_DELTA_FIELD(fragment_oom);
        FRAG_STATS_DELTA_FIELD(invalid_fragment_type);
        FRAG_STATS_DELTA_FIELD(fragments_sent);
        FRAG_STATS_DELTA_FIELD(fragments_resent);
    }
}

void robusto_fragment_stats_reset(void)
{
    memset(&fragment_stats, 0, sizeof(fragment_stats));
    memset(&fragment_stats_last_read, 0, sizeof(fragment_stats_last_read));
}

#undef FRAG_STATS_DELTA_FIELD

fragmented_message_t *get_last_frag_message()
{
    return last_frag_msg;
}

void remove_fragmented_message(fragmented_message_t *frag_msg)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (frag_msg->media_type == robusto_mt_halow && !frag_msg->is_receiver) {
        halow_feedback_end();
        robusto_free(frag_msg->received_fragments);
        robusto_free(frag_msg);
        return;
    }
#endif
    SLIST_REMOVE(&fragmented_messages_head, frag_msg, fragmented_message, fragmented_messages);
    if (last_frag_msg == frag_msg)
    {
        last_frag_msg = NULL;
    }
    if (frag_msg->receive_buffer != NULL)
    {
        robusto_free(frag_msg->receive_buffer);
    }
    robusto_free(frag_msg->received_fragments);
    robusto_free(frag_msg);
}

static void remove_stale_fragment_receives(uint32_t now)
{
    fragmented_message_t *frag_msg = SLIST_FIRST(&fragmented_messages_head);

    while (frag_msg != NULL)
    {
        fragmented_message_t *next = SLIST_NEXT(frag_msg, fragmented_messages);
        if (frag_msg->is_receiver &&
            (uint32_t)(now - frag_msg->start_time) > FRAG_SEND_TOTAL_WAIT_MS)
        {
            ROB_LOGW(fragmentation_log_prefix,
                     "Removing stale fragmented receive hash=%lu bytes=%lu age_ms=%lu.",
                     (unsigned long)frag_msg->hash,
                     (unsigned long)frag_msg->receive_buffer_length,
                     (unsigned long)(now - frag_msg->start_time));
            remove_fragmented_message(frag_msg);
        }
        frag_msg = next;
    }
}

static fragmented_message_t *find_fragmented_message(robusto_peer_t *peer,
                                                      e_media_type media_type,
                                                      uint32_t hash,
                                                      bool is_receiver, uint64_t transfer_id)
{
    if (last_frag_msg && last_frag_msg->peer == peer &&
        last_frag_msg->media_type == media_type && last_frag_msg->hash == hash &&
        last_frag_msg->is_receiver == is_receiver &&
        last_frag_msg->transfer_id == transfer_id)
    {
        ROB_LOGD(fragmentation_log_prefix, "Matched with cached frag");
        return last_frag_msg->abort_transmission ? NULL : last_frag_msg;
    }

    fragmented_message_t *curr = NULL;
    SLIST_FOREACH(curr, &fragmented_messages_head, fragmented_messages)
    {
        if (curr->peer == peer && curr->media_type == media_type &&
            curr->hash == hash && curr->is_receiver == is_receiver &&
            curr->transfer_id == transfer_id)
        {
            last_frag_msg = curr;
            return curr->abort_transmission ? NULL : curr;
        }
    }

    return NULL;
}

static void remove_abandoned_fragment_receive(robusto_peer_t *peer, e_media_type media_type)
{
    fragmented_message_t *frag_msg = SLIST_FIRST(&fragmented_messages_head);

    while (frag_msg != NULL)
    {
        fragmented_message_t *next = SLIST_NEXT(frag_msg, fragmented_messages);
        if (frag_msg->is_receiver && frag_msg->peer == peer && frag_msg->media_type == media_type)
        {
            ROB_LOGW(fragmentation_log_prefix,
                     "Replacing abandoned fragmented receive hash=%lu bytes=%lu.",
                     (unsigned long)frag_msg->hash,
                     (unsigned long)frag_msg->receive_buffer_length);
            remove_fragmented_message(frag_msg);
        }
        frag_msg = next;
    }
}

static completed_fragment_transfer_t *find_completed_fragment_transfer(robusto_peer_t *peer,
                                                                       e_media_type media_type,
                                                                       uint32_t hash,
                                                                       uint32_t now, uint64_t transfer_id)
{
    for (uint32_t index = 0; index < FRAG_COMPLETION_CACHE_SIZE; ++index)
    {
        completed_fragment_transfer_t *completed = &completed_fragment_transfers[index];
        uint32_t ttl = completed->media_type == robusto_mt_halow ? FRAG_SEND_TOTAL_WAIT_MS :
                                                       FRAG_COMPLETION_CACHE_TTL_MS;
        if (completed->valid && (uint32_t)(now - completed->completed_at) > ttl)
        {
            completed->valid = false;
        }
        if (completed->valid && completed->peer == peer && completed->media_type == media_type &&
            completed->hash == hash && completed->transfer_id == transfer_id)
        {
            return completed;
        }
    }

    return NULL;
}

static void remember_completed_fragment_transfer(robusto_peer_t *peer,
                                                  e_media_type media_type,
                                                  uint32_t hash,
                                                  rob_ret_val_t result, uint64_t transfer_id)
{
    uint32_t now = (uint32_t)r_millis();
    completed_fragment_transfer_t *slot = find_completed_fragment_transfer(peer, media_type, hash, now, transfer_id);
    if (slot == NULL)
    {
        uint32_t oldest_age = 0;
        for (uint32_t index = 0; index < FRAG_COMPLETION_CACHE_SIZE; ++index)
        {
            completed_fragment_transfer_t *candidate = &completed_fragment_transfers[index];
            if (!candidate->valid)
            {
                slot = candidate;
                break;
            }
            uint32_t age = (uint32_t)(now - candidate->completed_at);
            if (slot == NULL || age > oldest_age)
            {
                slot = candidate;
                oldest_age = age;
            }
        }
    }

    slot->peer = peer;
    slot->media_type = media_type;
    slot->hash = hash;
    slot->transfer_id = transfer_id;
    slot->result = result;
    slot->completed_at = now;
    slot->valid = true;
}

static rob_ret_val_t send_result_hash(robusto_peer_t *peer,
                                      uint32_t hash,
                                      rob_ret_val_t return_value,
                                      cb_send_message *send_message, e_media_type media_type,
                                      uint64_t transfer_id, uint32_t round)
{
    uint8_t *buffer = robusto_malloc(ROBUSTO_CRC_LENGTH + 4);
    if (buffer == NULL)
    {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    memcpy(buffer, &hash, 4);
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESULT;

    int16_t tmp_retval = return_value;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &tmp_retval, sizeof(int16_t));
    rob_ret_val_t rc = send_fragment_wire(peer, media_type, transfer_id, round, buffer,
                                         ROBUSTO_CRC_LENGTH + 2 + sizeof(int16_t), false, send_message);
    robusto_free(buffer);
    return rc;
}

static void replay_completed_fragment_result(robusto_peer_t *peer,
                                             const completed_fragment_transfer_t *completed,
                                             cb_send_message *send_message, uint32_t round)
{
    rob_ret_val_t rc = send_result_hash(peer, completed->hash, completed->result, send_message,
                                       completed->media_type, completed->transfer_id, round);
    if (rc != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix,
                 "Failed replaying completed fragment result hash=%lu result=%i rc=%i.",
                 (unsigned long)completed->hash,
                 completed->result,
                 rc);
    }
}
/**
 * @brief Send the result of a fragmentation message transmission
 *
 * @param peer
 * @param frag_msg
 * @param return_value
 * @param send_message
 */
static rob_ret_val_t send_result(robusto_peer_t *peer, fragmented_message_t *frag_msg, rob_ret_val_t return_value, cb_send_message *send_message)
{
    return send_result_hash(peer, frag_msg->hash, return_value, send_message,
                            frag_msg->media_type, frag_msg->transfer_id, frag_msg->recovery_round);
}

/**
 * @brief Handle a fragmentated message request
 *
 * @param peer
 * @param data
 * @param len
 * @param fragment_size
 */
void handle_frag_request(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message, uint64_t transfer_id)
{
    robusto_media_t *media = get_media_info(peer, media_type);
    // Manually check CRC32 hash

    if (len < ROBUSTO_CRC_LENGTH + 18)
    {
        fragment_stats_add(&fragment_stats.wrong_fragment_length, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because wrong length, %i.", len);
        add_to_history(media, false, ROB_FAIL);
        return;
    }

    if (*(uint32_t *)(data) != robusto_crc32(0, data + 4, 18))
    {
        fragment_stats_add(&fragment_stats.full_message_crc_mismatch, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        add_to_history(media, false, ROB_FAIL);
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because hash mismatch.");
        return;
    }

    fragment_stats_add(&fragment_stats.request_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);

    uint32_t hash;
    memcpy(&hash, data + ROBUSTO_CRC_LENGTH + 14, 4);
    uint32_t now = (uint32_t)r_millis();
    remove_stale_fragment_receives(now);

    completed_fragment_transfer_t *completed = find_completed_fragment_transfer(peer, media_type, hash, now, transfer_id);
    if (completed != NULL)
    {
        ROB_LOGD(fragmentation_log_prefix,
                 "Replaying completed fragment result for duplicate request hash=%lu result=%i.",
                 (unsigned long)hash,
                 completed->result);
        replay_completed_fragment_result(peer, completed, send_message, 0);
        media->last_receive = now;
        return;
    }

    uint32_t receive_buffer_length;
    uint32_t fragment_count;
    uint32_t requested_fragment_size;
    memcpy(&receive_buffer_length, data + ROBUSTO_CRC_LENGTH + 2, 4);
    memcpy(&fragment_count, data + ROBUSTO_CRC_LENGTH + 6, 4);
    memcpy(&requested_fragment_size, data + ROBUSTO_CRC_LENGTH + 10, 4);

    if (receive_buffer_length == 0 || requested_fragment_size == 0 ||
        (media_type == robusto_mt_halow && requested_fragment_size > fragment_size) ||
        fragment_count != receive_buffer_length / requested_fragment_size +
                          (receive_buffer_length % requested_fragment_size != 0)) {
        ROB_LOGE(fragmentation_log_prefix, "Invalid fragment request dimensions");
        return;
    }
    fragmented_message_t *frag_msg = find_fragmented_message(peer, media_type, hash, true, transfer_id);
    bool new_fragment_message = false;
    if (!frag_msg)
    {
        frag_msg = robusto_malloc(sizeof(fragmented_message_t));
        if (frag_msg == NULL)
        {
            fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because fragmented message allocation failed.");
            add_to_history(media, false, ROB_ERR_OUT_OF_MEMORY);
            return;
        }
        memset(frag_msg, 0, sizeof(fragmented_message_t));
        frag_msg->peer = peer;
        frag_msg->media_type = media_type;
        frag_msg->is_receiver = true;
        frag_msg->last_requested = FRAG_NO_REQUESTED_FRAGMENT;
        frag_msg->receive_buffer_length = receive_buffer_length;
        frag_msg->fragment_count = fragment_count;
        frag_msg->fragment_size = requested_fragment_size;
        frag_msg->hash = hash;
        frag_msg->transfer_id = transfer_id;
        new_fragment_message = true;
    }
    else
    {
        if (frag_msg->receive_buffer_length != receive_buffer_length ||
            frag_msg->fragment_count != fragment_count ||
            frag_msg->fragment_size != requested_fragment_size)
        {
            fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            ROB_LOGE(fragmentation_log_prefix,
                     "Duplicate fragment request metadata mismatch for hash %lu.",
                     (unsigned long)hash);
            return;
        }
        if (frag_msg->receive_buffer != NULL)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Duplicate fragment request ignored for active hash %lu.",
                     (unsigned long)hash);
            if (media_type != robusto_mt_halow) frag_msg->start_time = (uint32_t)r_millis();
            media->last_receive = r_millis();
            return;
        }
    }

    // TODO: How big should we allow before SPIRAM and more?
    remove_abandoned_fragment_receive(peer, media_type);

    uint8_t *receive_buffer = robusto_malloc(frag_msg->receive_buffer_length);
    uint8_t *received_fragments = frag_msg->received_fragments;
    if (received_fragments == NULL)
    {
        received_fragments = robusto_malloc(frag_msg->fragment_count);
    }
    if (receive_buffer == NULL || received_fragments == NULL)
    {
        fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix,
                 "Fragmented request failed allocating %lu-byte receive buffer and %lu-byte fragment map.",
                 (unsigned long)frag_msg->receive_buffer_length,
                 (unsigned long)frag_msg->fragment_count);
        if (receive_buffer != NULL)
        {
            robusto_free(receive_buffer);
        }
        if (frag_msg->received_fragments == NULL && received_fragments != NULL)
        {
            robusto_free(received_fragments);
        }
        if (new_fragment_message)
        {
            robusto_free(frag_msg);
        }
        add_to_history(media, false, ROB_ERR_OUT_OF_MEMORY);
        return;
    }
    frag_msg->receive_buffer = receive_buffer;
    frag_msg->received_fragments = received_fragments;
    frag_msg->abort_transmission = false;
    frag_msg->state = ROB_ST_RUNNING;
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);
    if (new_fragment_message)
    {
        SLIST_INSERT_HEAD(&fragmented_messages_head, frag_msg, fragmented_messages);
    }
    ROB_LOGD(fragmentation_log_prefix, "Fragmented initialization received, info:\n \
        data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
             frag_msg->receive_buffer_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);
    /* When the frag_msg was created, a non-used element */
    frag_msg->start_time = (uint32_t)r_millis();

    last_frag_msg = frag_msg;
    media->last_receive = r_millis();
    if (media_type == robusto_mt_halow) {
        ROB_LOGI(fragmentation_log_prefix,
                 "HaLow RX transfer request hash=%lu bytes=%lu fragments=%lu chunk=%lu id=%016llx",
                 (unsigned long)frag_msg->hash, (unsigned long)frag_msg->receive_buffer_length,
                 (unsigned long)frag_msg->fragment_count, (unsigned long)frag_msg->fragment_size,
                 (unsigned long long)frag_msg->transfer_id);
    }
}

void check_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message, bool send_resend_request)
{

    // Check that we have no missing parts, we do this extra pass to save memory
    uint32_t missing_fragments = 0;
    uint32_t last_missing = FRAG_NO_REQUESTED_FRAGMENT;

    for (uint32_t pass_one_count = 0; pass_one_count < frag_msg->fragment_count; pass_one_count++)
    {
        if (frag_msg->received_fragments[pass_one_count] == 0)
        {
            missing_fragments++;
            last_missing = pass_one_count;
        }
    }
    if (missing_fragments > 0)
    {
        if (!send_resend_request)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Last fragment arrived with %lu missing fragment(s); waiting for in-flight fragments before requesting resend. first_missing=%lu last_missing=%lu hash=%lu",
                     (unsigned long)missing_fragments,
                     (unsigned long)fragment_first_missing(frag_msg),
                     (unsigned long)last_missing,
                     (unsigned long)frag_msg->hash);
            return;
        }
        frag_msg->last_requested = last_missing;
        // Gather missing fragments into an array
        fragment_stats_add(&fragment_stats.resend_request_sent, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        fragment_stats_add(&fragment_stats.missing_fragments_reported, missing_fragments, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGW(fragmentation_log_prefix,
             "We have %lu missing fragment(s). first_missing=%lu last_missing=%lu hash=%lu",
               (unsigned long)missing_fragments,
               (unsigned long)fragment_first_missing(frag_msg),
               (unsigned long)frag_msg->last_requested,
               (unsigned long)frag_msg->hash);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
        if (media_type == robusto_mt_halow && missing_fragments == frag_msg->fragment_count) {
            robusto_halow_rx_stats_t rx = {0};
            rob_ret_val_t spi_result = robusto_halow_get_rx_stats(&rx);
            ROB_LOGW(fragmentation_log_prefix,
                     "HaLow RX all-missing hash=%lu scope=totals stats_level=%u rx_req=%lu rx_msg=%lu invalid_ref=%lu invalid_index=%lu wrong_len=%lu crc=%lu oom=%lu spi_rc=%d spi_frames=%lu spi_unready=%lu envelopes=%lu",
                     (unsigned long)frag_msg->hash, (unsigned)fragment_stats_level,
                     (unsigned long)fragment_stats.request_received,
                     (unsigned long)fragment_stats.message_received,
                     (unsigned long)fragment_stats.invalid_fragment_reference,
                     (unsigned long)fragment_stats.invalid_fragment_index,
                     (unsigned long)fragment_stats.wrong_fragment_length,
                     (unsigned long)fragment_stats.full_message_crc_mismatch,
                     (unsigned long)fragment_stats.fragment_oom, spi_result,
                     (unsigned long)rx.spi_frames, (unsigned long)rx.spi_unready,
                     (unsigned long)rx.envelopes);
        }
#endif
        uint8_t *missing = robusto_malloc(ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        if (missing == NULL) {
            ROB_LOGE(fragmentation_log_prefix, "Fragment feedback allocation failed");
            return;
        }
        memcpy(missing, &frag_msg->hash, 4);
        missing[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
        missing[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESEND;

        for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
        {
            missing[ROBUSTO_CRC_LENGTH + 2 + missing_counter] = frag_msg->received_fragments[missing_counter];
        }
        rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        rob_ret_val_t feedback_rc = send_fragment_wire(peer, media_type, frag_msg->transfer_id,
            frag_msg->recovery_round, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count,
            false, send_message);
        if (feedback_rc != ROB_OK) {
            ROB_LOGE(fragmentation_log_prefix, "Fragment feedback write failed rc=%i", feedback_rc);
        }
        robusto_free(missing);
        return;
    }
    else
    {
        // Last, check hash and reply with result
        if (frag_msg->hash != robusto_crc32(0, frag_msg->receive_buffer, frag_msg->receive_buffer_length))
        {
            fragment_stats_add(&fragment_stats.full_message_crc_mismatch, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            ROB_LOGE(fragmentation_log_prefix, "The full message did not match with the hash");
            remember_completed_fragment_transfer(peer, media_type, frag_msg->hash, ROB_ERR_WRONG_CRC, frag_msg->transfer_id);
            rob_ret_val_t result_rc = send_result(peer, frag_msg, ROB_ERR_WRONG_CRC, send_message);
            if (result_rc != ROB_OK)
            {
                ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment CRC result hash=%lu rc=%i", (unsigned long)frag_msg->hash, result_rc);
            }
            remove_fragmented_message(frag_msg);
            // TODO: Send check result failed. We have no way of knowing which part failed.
            return;
        }
        else
        {
            ROB_LOGD(fragmentation_log_prefix, "The assembled %lu-byte multimessage matched the hash, passing to incoming.", frag_msg->receive_buffer_length);
            // rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, frag_msg->receive_buffer, frag_msg->receive_buffer_length > 100 ? 100:frag_msg->receive_buffer_length);
            if (media_type != robusto_mt_halow) {
                remember_completed_fragment_transfer(peer, media_type, frag_msg->hash, ROB_OK, frag_msg->transfer_id);
                rob_ret_val_t result_rc = send_result(peer, frag_msg, ROB_OK, send_message);
                if (result_rc != ROB_OK) {
                    ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment OK result hash=%lu rc=%i", (unsigned long)frag_msg->hash, result_rc);
                }
            }
            ROB_LOGD(fragmentation_log_prefix,
                     "Fragment receive complete hash=%lu bytes=%lu fragments=%lu",
                     (unsigned long)frag_msg->hash,
                     (unsigned long)frag_msg->receive_buffer_length,
                     (unsigned long)frag_msg->fragment_count);

            rob_ret_val_t incoming_result = robusto_handle_incoming(
                frag_msg->receive_buffer, frag_msg->receive_buffer_length, peer, media_type, 0);
            add_to_history(get_media_info(peer, media_type), false, incoming_result);
            if (media_type == robusto_mt_halow) {
                remember_completed_fragment_transfer(peer, media_type, frag_msg->hash, incoming_result, frag_msg->transfer_id);
                rob_ret_val_t result_rc = send_result(peer, frag_msg, incoming_result, send_message);
                if (result_rc != ROB_OK) {
                    ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment handoff result hash=%lu rc=%i", (unsigned long)frag_msg->hash, result_rc);
                }
                if (incoming_result == ROB_OK) {
                    ROB_LOGI(fragmentation_log_prefix,
                             "HaLow RX reassembled hash=%lu bytes=%lu fragments=%lu incoming_rc=%d id=%016llx",
                             (unsigned long)frag_msg->hash, (unsigned long)frag_msg->receive_buffer_length,
                             (unsigned long)frag_msg->fragment_count, incoming_result,
                             (unsigned long long)frag_msg->transfer_id);
                } else {
                    ROB_LOGE(fragmentation_log_prefix,
                             "HaLow RX incoming handoff failed hash=%lu bytes=%lu rc=%d",
                             (unsigned long)frag_msg->hash, (unsigned long)frag_msg->receive_buffer_length,
                             incoming_result);
                }
            }
            frag_msg->receive_buffer = NULL;
            remove_fragmented_message(frag_msg);
            return;
        }
        
    }
}
void handle_frag_message(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message, uint64_t transfer_id, uint32_t round)
{
    // Initiate a new fragmented  (...stream?)
    fragment_stats_add(&fragment_stats.message_received, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
    ROB_LOGD(fragmentation_log_prefix, "handle_frag_message (hash %lu)", *(uint32_t *)data);
    if (len > ROBUSTO_CRC_LENGTH + 18)
    {

        rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, data, ROBUSTO_CRC_LENGTH + 18);
    }
    else
    {
        rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, data, len);
    }
    uint32_t hash;
    memcpy(&hash, data, 4);
    fragmented_message_t *frag_msg = find_fragmented_message(peer, media_type, hash, true, transfer_id);
    if (!frag_msg)
    {
        completed_fragment_transfer_t *completed = find_completed_fragment_transfer(
            peer, media_type, hash, (uint32_t)r_millis(), transfer_id);
        if (completed != NULL && media_type == robusto_mt_halow) {
            replay_completed_fragment_result(peer, completed, send_message, round);
            return;
        }
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGW(fragmentation_log_prefix, "Invalid fragment reference.");
        return;
    }
    if (media_type == robusto_mt_halow && round > frag_msg->recovery_round) {
        ROB_LOGE(fragmentation_log_prefix, "Fragment belongs to an unrequested recovery round");
        return;
    }

    uint32_t msg_frag_count;
    memcpy(&msg_frag_count, data + ROBUSTO_CRC_LENGTH + 2, 4);

    if (msg_frag_count >= frag_msg->fragment_count)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_index, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Invalid fragment index %lu, fragment count %lu.", msg_frag_count, frag_msg->fragment_count);
        return;
    }

    // We check the length of all fragments, start with calculating what the current should be
    uint32_t curr_frag_size = frag_msg->fragment_size;
    if (msg_frag_count == frag_msg->fragment_count - 1)
    {
        // If it is the last fragment, it is whatever is left
        curr_frag_size = frag_msg->receive_buffer_length - (frag_msg->fragment_size * msg_frag_count);
    }

    uint32_t expected_message_length = FRAG_HEADER_LEN + curr_frag_size;
    ROB_LOGD(fragmentation_log_prefix, "Received part %lu (of %lu), length %lu bytes.", msg_frag_count + 1, frag_msg->fragment_count, curr_frag_size);
    if (expected_message_length != len)
    {
        fragment_stats_add(&fragment_stats.wrong_fragment_length, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of fragment %lu: %i bytes, expected %lu", msg_frag_count, len, expected_message_length);
        return;
    }

    // Length of the data checks out
    ROB_LOGD(fragmentation_log_prefix, "Storing fragment: %lu. Offset: %lu, length: %i", msg_frag_count, fragment_size * msg_frag_count, len - FRAG_HEADER_LEN);
    bool first_reception = frag_msg->received_fragments[msg_frag_count] == 0;
    memcpy(frag_msg->receive_buffer + (frag_msg->fragment_size * msg_frag_count), data + FRAG_HEADER_LEN, len - FRAG_HEADER_LEN);
    frag_msg->received_fragments[msg_frag_count] = 1;
    if (media_type == robusto_mt_halow && first_reception &&
        (msg_frag_count == 0 || (msg_frag_count + 1U) % 32U == 0 ||
         msg_frag_count == frag_msg->fragment_count - 1U)) {
        ROB_LOGI(fragmentation_log_prefix,
                 "HaLow RX fragment stored hash=%lu index=%lu count=%lu bytes=%lu",
                 (unsigned long)frag_msg->hash, (unsigned long)msg_frag_count,
                 (unsigned long)frag_msg->fragment_count, (unsigned long)curr_frag_size);
    }

    // Are we at the last, or last requested, fragment, no less?
    if ((media_type == robusto_mt_halow && fragment_missing_count(frag_msg) == 0) ||
        (frag_msg->last_requested != FRAG_NO_REQUESTED_FRAGMENT && msg_frag_count == frag_msg->last_requested) ||
        (msg_frag_count == frag_msg->fragment_count - 1))
    {
        uint32_t missing_after_store = fragment_missing_count(frag_msg);
        uint32_t first_missing = fragment_first_missing(frag_msg);

        if (missing_after_store == 0)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Fragment boundary complete index=%lu hash=%lu state=%u",
                     (unsigned long)msg_frag_count,
                     (unsigned long)frag_msg->hash,
                     frag_msg->state);
        }
        else
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Fragment boundary pending index=%lu hash=%lu missing_after_store=%lu first_missing=%lu last_requested=%lu state=%u",
                     (unsigned long)msg_frag_count,
                     (unsigned long)frag_msg->hash,
                     (unsigned long)missing_after_store,
                     (unsigned long)first_missing,
                     (unsigned long)frag_msg->last_requested,
                     frag_msg->state);
        }
        ROB_LOGD(fragmentation_log_prefix, "We are on the last fragment (initial send or re-send)");
        // If received fragments doesn't add up to fragment count, send list of missing fragments to sender
        // Note that w  e probably do not want to handle larger data typically, even though SPIRAM may support it. A larger ESP-NOW frame size would change that though.

        bool is_requested_fragment = frag_msg->last_requested != FRAG_NO_REQUESTED_FRAGMENT && msg_frag_count == frag_msg->last_requested;
        check_fragments(peer, media_type, frag_msg, send_message,
                        media_type != robusto_mt_halow && is_requested_fragment);
    }
    ROB_LOGD(fragmentation_log_prefix, "Returning from handle_frag_message");
}

static rob_ret_val_t send_fragment_packet(robusto_peer_t *peer, e_media_type media_type,
                                          fragmented_message_t *frag_msg, uint8_t *data,
                                          uint32_t length, bool receipt, cb_send_message *send_message)
{
    rob_ret_val_t result = send_fragment_wire(peer, media_type, frag_msg->transfer_id,
                                             frag_msg->recovery_round, data, length, receipt, send_message);
    if (media_type != robusto_mt_halow || result != ROB_ERR_QUEUE_FULL) return result;
    uint32_t waiting_since = (uint32_t)r_millis();
    ROB_LOGW(fragmentation_log_prefix,
             "Waiting for HaLow credits hash=%lu subtype=%u; retaining unsent packet",
             (unsigned long)frag_msg->hash, (unsigned)data[ROBUSTO_CRC_LENGTH + 1]);
    do {
        send_wait_yield(media_type);
        if ((uint32_t)(r_millis() - frag_msg->start_time) >= FRAG_SEND_TOTAL_WAIT_MS) {
            ROB_LOGE(fragmentation_log_prefix, "HaLow transfer deadline exceeded waiting for credits");
            frag_msg->state = ROB_ST_TIMED_OUT;
            return ROB_ERR_TIMEOUT;
        }
        result = send_fragment_wire(peer, media_type, frag_msg->transfer_id,
                                     frag_msg->recovery_round, data, length, receipt, send_message);
    } while (result == ROB_ERR_QUEUE_FULL);
    if (result == ROB_OK) {
        ROB_LOGI(fragmentation_log_prefix,
                 "HaLow credits available after %lu ms; submitted retained packet subtype=%u",
                 (unsigned long)((uint32_t)r_millis() - waiting_since),
                 (unsigned)data[ROBUSTO_CRC_LENGTH + 1]);
    }
    return result;
}

void send_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message)
{

    ROB_LOGD(fragmentation_log_prefix, "Sending %" PRIu32 " fragments:", frag_msg->fragment_count);
    
    uint8_t *buffer = NULL;
    uint32_t curr_frag_size = curr_frag_size = frag_msg->fragment_size;

    robusto_media_t *info = get_media_info(peer, media_type);

    // Allocate a buffer big enough for the largest fragment
    buffer = robusto_malloc(frag_msg->fragment_size + FRAG_HEADER_LEN);
    if (buffer == NULL) {
        ROB_LOGE(fragmentation_log_prefix, "Fragment send buffer allocation failed");
        fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        frag_msg->state = ROB_ST_FAILED;
        return;
    }
    // We always send the same hash, as an identifier
    memcpy(buffer, &frag_msg->hash, 4);
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_MESSAGE;

    for (uint32_t curr_fragment = 0; curr_fragment < frag_msg->fragment_count; curr_fragment++)
    {
        if (media_type == robusto_mt_halow &&
            (uint32_t)(r_millis() - frag_msg->start_time) >= FRAG_SEND_TOTAL_WAIT_MS) {
            ROB_LOGE(fragmentation_log_prefix, "HaLow transfer deadline exceeded during fragment submission");
            frag_msg->state = ROB_ST_TIMED_OUT;
            robusto_free(buffer);
            return;
        }
        if (frag_msg->abort_transmission)
        {
            robusto_free(buffer);
            return;
        }
        if (media_type != robusto_mt_halow && frag_msg->state == ROB_ST_RETRYING &&
            frag_msg->retry_start_time != 0U &&
            (uint32_t)(r_millis() - frag_msg->retry_start_time) > FRAG_RETRY_TOTAL_WAIT_MS)
        {
            ROB_LOGE(fragmentation_log_prefix,
                     "Fragmented message retry send loop exceeded retry timeout (%lu ms), closing the transmission.",
                     (uint32_t)FRAG_RETRY_TOTAL_WAIT_MS);
            frag_msg->abort_transmission = true;
            frag_msg->state = ROB_ST_TIMED_OUT;
            robusto_free(buffer);
            return;
        }
        // QoS will disturb sending like this
        info->postpone_qos = true;

        if (frag_msg->received_fragments[curr_fragment] == 1)
        {
            ROB_LOGD(fragmentation_log_prefix, "Skipping part %lu", curr_fragment);
            continue;
        }
        // Counter
        memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &curr_fragment, sizeof(curr_fragment));

        // If it is the last part, send only the remaining data
        if (curr_fragment == (frag_msg->fragment_count - 1))
        {
            curr_frag_size = frag_msg->send_data_length - (frag_msg->fragment_size * curr_fragment);
        }

        if (frag_msg->state == ROB_ST_RETRYING)
        {
            ROB_LOGD(fragmentation_log_prefix, "Re-sending fragment %lu (of %lu), pos %lu, length %lu bytes of (%lu total bytes).",
                     curr_fragment + 1, frag_msg->fragment_count, frag_msg->fragment_size * curr_fragment, curr_frag_size, frag_msg->send_data_length);
        }
        else
        {
            ROB_LOGD(fragmentation_log_prefix, "Sending fragment %lu (of %lu), pos %lu, length %lu bytes of (%lu total bytes).",
                     curr_fragment + 1, frag_msg->fragment_count, frag_msg->fragment_size * curr_fragment, curr_frag_size, frag_msg->send_data_length);
        }
        memcpy(buffer + FRAG_HEADER_LEN, frag_msg->send_data + (frag_msg->fragment_size * curr_fragment), curr_frag_size);

        if (curr_fragment == 10U)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "About to send fragment index=10 hash=%lu state=%u len=%lu total_frags=%lu total_bytes=%lu",
                     (unsigned long)frag_msg->hash,
                     frag_msg->state,
                     (unsigned long)curr_frag_size,
                     (unsigned long)frag_msg->fragment_count,
                     (unsigned long)frag_msg->send_data_length);
        }

        if (SKIP_FRAGMENT_TEST)
        {
            if (media_type != robusto_mt_halow && frag_msg->state == ROB_ST_RETRYING)
            {
                fragment_stats_add(&fragment_stats.fragments_resent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
            }
            else if (media_type != robusto_mt_halow)
            {
                fragment_stats_add(&fragment_stats.fragments_sent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
            }

            rob_ret_val_t send_retval = send_fragment_packet(
                peer, media_type, frag_msg, buffer, FRAG_HEADER_LEN + curr_frag_size, false, send_message);
            if (media_type == robusto_mt_halow && send_retval == ROB_OK) {
                fragment_stats_add(frag_msg->state == ROB_ST_RETRYING ?
                                   &fragment_stats.fragments_resent : &fragment_stats.fragments_sent,
                                   1U, ROBUSTO_STATS_LEVEL_VERBOSE);
            }
            if (curr_fragment == 10U)
            {
                ROB_LOGD(fragmentation_log_prefix,
                         "Sent fragment index=10 hash=%lu state=%u retval=%d len=%lu total_frags=%lu total_bytes=%lu",
                         (unsigned long)frag_msg->hash,
                         frag_msg->state,
                         send_retval,
                         (unsigned long)curr_frag_size,
                         (unsigned long)frag_msg->fragment_count,
                         (unsigned long)frag_msg->send_data_length);
            }
            if (send_retval != ROB_OK)
            {
                if (media_type == robusto_mt_halow) {
                    ROB_LOGE(fragmentation_log_prefix, "Stopping HaLow fragment burst at index=%lu hash=%lu rc=%i",
                             (unsigned long)curr_fragment, (unsigned long)frag_msg->hash, send_retval);
                    if (frag_msg->state != ROB_ST_TIMED_OUT) frag_msg->state = ROB_ST_FAILED;
                    robusto_free(buffer);
                    return;
                }
                ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment [%" PRIu32 "].", curr_fragment);
            }
        }
        else
        {
            ROB_LOGW(fragmentation_log_prefix,
                     "Fragment %lu intentionally skipped by CONFIG_ROBUSTO_TESTING_SKIP_NTH_FRAGMENT=%d while state=%u hash=%lu",
                     (unsigned long)curr_fragment,
                     SKIP_FRAGMENT_INDEX,
                     frag_msg->state,
                     (unsigned long)frag_msg->hash);
        }

    }
    robusto_free(buffer);
    // The response may be really quick, so we only set to done if none of the result states are set
    if (frag_msg->state < ROB_ST_DONE) {
        frag_msg->state = ROB_ST_DONE;
    }
    
}

/**
 * @brief Handle the result message
 *
 * @param peer
 * @param media
 * @param data
 * @param len
 * @param fragment_size
 * @param send_message
 */

void handle_frag_resend(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message, uint64_t transfer_id, uint32_t round)
{
    fragment_stats_add(&fragment_stats.resend_request_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) {
        halow_record_feedback(peer, data, len, true, transfer_id, round);
        return;
    }
#endif
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_resend");
    robusto_media_t *media = get_media_info(peer, media_type);

    fragmented_message_t *frag_msg = find_fragmented_message(peer, media_type, *(uint32_t *)data, false, 0);
    if (!frag_msg)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_resend, fragment count: %lu ", frag_msg->fragment_count);
    if (frag_msg->retry_start_time == 0U)
    {
        frag_msg->retry_start_time = (uint32_t)r_millis();
    }
    frag_msg->state = ROB_ST_RETRYING;
    if (len - (ROBUSTO_CRC_LENGTH + 2) != frag_msg->fragment_count)
    {
        fragment_stats_add(&fragment_stats.wrong_resend_map_length, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of missing fragments data: %i. Expected: %lu.", len - (ROBUSTO_CRC_LENGTH + 2), frag_msg->fragment_count);
        media->receive_failures++;
        return;
    }
    media->receive_successes++;
    frag_msg->state = ROB_ST_RETRYING;
    memcpy(frag_msg->received_fragments, data + ROBUSTO_CRC_LENGTH + 2, len - (ROBUSTO_CRC_LENGTH + 2));
    ROB_LOGW(fragmentation_log_prefix,
             "Resend requested hash=%lu missing_before_resend=%lu first_missing=%lu last_requested=%lu",
             (unsigned long)frag_msg->hash,
             (unsigned long)fragment_missing_count(frag_msg),
             (unsigned long)fragment_first_missing(frag_msg),
             (unsigned long)frag_msg->last_requested);
    send_fragments(peer, media_type, frag_msg, send_message);
}

/**
 * @brief Do a check
 *
 * @param peer
 * @param media
 * @param data
 * @param len
 * @param fragment_size
 * @param send_message
 */

void handle_frag_check(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message, uint64_t transfer_id, uint32_t round)
{
    fragment_stats_add(&fragment_stats.check_received, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_check");
    fragmented_message_t *frag_msg = find_fragmented_message(peer, media_type, *(uint32_t *)data, true, transfer_id);
    if (!frag_msg)
    {
        uint32_t hash;
        memcpy(&hash, data, sizeof(hash));
        completed_fragment_transfer_t *completed = find_completed_fragment_transfer(peer,
                                                                                     media_type,
                                                                                     hash,
                                                                                     (uint32_t)r_millis(), transfer_id);
        if (completed != NULL)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Replaying completed fragment result for status check hash=%lu result=%i.",
                     (unsigned long)hash,
                     completed->result);
            replay_completed_fragment_result(peer, completed, send_message, round);
            return;
        }
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    else
    {
        if (media_type == robusto_mt_halow) {
            if (round < frag_msg->recovery_round) {
                ROB_LOGD(fragmentation_log_prefix, "Ignoring stale HaLow status check");
                return;
            }
            frag_msg->recovery_round = round;
        }
        check_fragments(peer, media_type, frag_msg, send_message, true);
    }
}

/**
 * @brief Handle the success message
 *
 * @param peer
 * @param media
 * @param data
 * @param len
 * @param fragment_size
 * @param send_message
 */

static void apply_fragment_result(fragmented_message_t *frag_msg, int16_t result)
{
    ROB_LOGD(fragmentation_log_prefix, "result: %" PRIi16, result);
    switch (result)
    {
    case ROB_OK:
        fragment_stats_add(&fragment_stats.result_ok_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGD(fragmentation_log_prefix, "Receiver reports successful transmission, frag start time: %lu.", frag_msg->start_time);
        frag_msg->state = ROB_ST_SUCCEEDED;
        break;
    case ROB_FAIL:
        fragment_stats_add(&fragment_stats.result_fail_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports unsuccessful transmission, frag start time: %lu.", frag_msg->start_time);
        frag_msg->state = ROB_ST_FAILED;
        break;
    case ROB_ERR_WRONG_CRC:
        fragment_stats_add(&fragment_stats.result_fail_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports CRC mismatch");
        frag_msg->state = ROB_ST_FAILED;
        break;
    case ROB_ERR_TIMEOUT:
        fragment_stats_add(&fragment_stats.result_fail_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports timeout ");
        frag_msg->state = ROB_ST_TIMED_OUT;
        break;
    default:
        fragment_stats_add(&fragment_stats.result_fail_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        if (frag_msg->media_type == robusto_mt_halow) {
            ROB_LOGE(fragmentation_log_prefix, "Receiver reports fragmented-message failure rc=%i", result);
            frag_msg->state = ROB_ST_FAILED;
        } else {
            ROB_LOGE(fragmentation_log_prefix, "Unhandled fragmentation result code: %i", result);
        }
        break;
    }
}

void handle_frag_result(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message, uint64_t transfer_id, uint32_t round)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) {
        halow_record_feedback(peer, data, len, false, transfer_id, round);
        return;
    }
#endif
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_result");
    rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, data, len);
    fragmented_message_t *frag_msg = find_fragmented_message(peer, media_type, *(uint32_t *)data, false, 0);
    if (frag_msg == NULL) {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    int16_t result;
    memcpy(&result, data + ROBUSTO_CRC_LENGTH + 2, sizeof(result));
    apply_fragment_result(frag_msg, result);
}

#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
static bool halow_consume_feedback(fragmented_message_t *message)
{
    if (!halow_feedback_lock()) return false;
    bool result_pending = halow_feedback.result_pending;
    bool resend_pending = halow_feedback.resend && !result_pending;
    int16_t result = halow_feedback.result;
    if (resend_pending) {
        for (uint32_t i = 0; i < message->fragment_count; ++i) {
            message->received_fragments[i] |= halow_feedback.map[i];
        }
        message->state = ROB_ST_RETRYING;
    }
    halow_feedback.resend = false;
    halow_feedback.result_pending = false;
    if (result_pending) {
        unsigned slot = halow_completed_feedback_next;
        halow_completed_feedback_next = (slot + 1U) % FRAG_COMPLETION_CACHE_SIZE;
        halow_completed_feedback[slot].peer = halow_feedback.peer;
        halow_completed_feedback[slot].hash = halow_feedback.hash;
        halow_completed_feedback[slot].transfer_id = halow_feedback.transfer_id;
        halow_completed_feedback[slot].round = halow_feedback.round;
        halow_completed_feedback[slot].result = result;
        halow_completed_feedback[slot].completed_at = (uint32_t)r_millis();
    }
    halow_feedback_unlock();
    if (resend_pending) {
        ROB_LOGW(fragmentation_log_prefix,
                 "Applying HaLow resend feedback hash=%lu missing=%lu first_missing=%lu id=%016llx round=%lu on outgoing worker",
                 (unsigned long)message->hash, (unsigned long)fragment_missing_count(message),
                 (unsigned long)fragment_first_missing(message),
                 (unsigned long long)message->transfer_id, (unsigned long)message->recovery_round);
    }
    if (result_pending) apply_fragment_result(message, result);
    return true;
}
#endif

/**
 * @brief Handled fragmented messages
 *
 * @param peer The sending peer
 * @param data The fragment data
 * @param len The length of the data
 * @param fragment_size The size of the fragment of the media
 */
bool handle_fragmented(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    bool receipt = false;
    uint64_t transfer_id = 0;
    uint32_t round = 0;
    if (data == NULL || len < 6) {
        ROB_LOGE(fragmentation_log_prefix, "Truncated fragmented packet");
        robusto_free(data);
        return false;
    }
    if (media_type == robusto_mt_halow) {
        uint8_t type = data[5] & ~HALOW_FRAGMENT_V2;
        if (!(data[5] & HALOW_FRAGMENT_V2) || len < 18) {
            ROB_LOGE(fragmentation_log_prefix, "HaLow fragmentation v2 required; update both peers");
            robusto_free(data);
            return false;
        }
        transfer_id = fragment_read_id(data + 6);
        round = fragment_read_round(data + 14);
        bool shape_ok = (type == FRAG_REQUEST && len == 34 && round == 0) ||
                        (type == FRAG_MESSAGE && len >= 22) ||
                        (type == FRAG_CHECK && len == 18 && round != 0) ||
                        (type == FRAG_RESULT && len == 20) ||
                        (type == FRAG_RESEND && len >= 19 && round != 0);
        if (transfer_id == 0 || !shape_ok) {
            ROB_LOGE(fragmentation_log_prefix, "Malformed HaLow v2 fragment header/type/length");
            robusto_free(data);
            return false;
        }
        data[5] = type;
        memmove(data + 6, data + 18, (size_t)len - 18U);
        len -= HALOW_FRAGMENT_EXTENSION;
    }
    switch (data[ROBUSTO_CRC_LENGTH + 1])
    {
    case FRAG_REQUEST:
        handle_frag_request(peer, media_type, data, len, fragment_size, send_message, transfer_id);
        receipt = true;
        break;
    case FRAG_MESSAGE:
        handle_frag_message(peer, media_type, data, len, fragment_size, send_message, transfer_id, round);
        break;
    case FRAG_RESEND:
        handle_frag_resend(peer, media_type, data, len, fragment_size, send_message, transfer_id, round);
        break;
    case FRAG_RESULT:
        handle_frag_result(peer, media_type, data, len, fragment_size, send_message, transfer_id, round);
        break;
    case FRAG_CHECK:
        handle_frag_check(peer, media_type, data, len, fragment_size, send_message, transfer_id, round);
        break;
    default:
        fragment_stats_add(&fragment_stats.invalid_fragment_type, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Invalid fragment type byte: %hu", data[ROBUSTO_CRC_LENGTH + 1]);
    }

    // Postpone QoS so it doesn't interfere with long transmissions.
    get_media_info(peer, media_type)->postpone_qos = true;
    // The data must be freeable.

    robusto_free(data);
    return receipt;
}

rob_ret_val_t send_frag_check(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message)
{

#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) {
        if (!halow_feedback_lock()) return ROB_ERR_MUTEX;
        ++frag_msg->recovery_round;
        halow_feedback.round = frag_msg->recovery_round;
        halow_feedback.round_open = true;
        halow_feedback_unlock();
    }
#endif
    fragment_stats_add(&fragment_stats.check_sent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
    ROB_LOGD(fragmentation_log_prefix, "In send_frag_check");
    uint8_t *msg_frag_check = robusto_malloc(ROBUSTO_CRC_LENGTH + 2);
    if (msg_frag_check == NULL)
    {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    memcpy(msg_frag_check, &frag_msg->hash, 4);
    msg_frag_check[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    msg_frag_check[ROBUSTO_CRC_LENGTH + 1] = FRAG_CHECK;

    rob_ret_val_t rc = send_fragment_packet(
        peer, media_type, frag_msg, msg_frag_check, ROBUSTO_CRC_LENGTH + 2, false, send_message);
    // TODO: We should probably free data here as well. And duplicate the data in the test send_message call back.
    robusto_free(msg_frag_check);
    return rc;
}

/**
 * @brief Sends a fragmented message
 */
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, uint32_t fragment_size, cb_send_message *send_message)
{
    int rc = ROB_FAIL;
    robusto_media_t *media = get_media_info(peer, media_type);
    fragment_stats_add(&fragment_stats.send_started, 1U, ROBUSTO_STATS_LEVEL_BASIC);
    // How many parts will it have?
    uint32_t fragment_count = data_length / fragment_size;
    if (data_length % fragment_size > 0)
    {
        fragment_count++;
    }

    ROB_LOGD(fragmentation_log_prefix, "Creating and sending a %lu-part, %lu-byte fragmented message in %lu-byte chunks.", fragment_count, data_length, fragment_size);
    //   rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, data_length);
    uint32_t curr_part = 0;
    uint8_t *buffer = robusto_malloc(fragment_size + sizeof(curr_part));
    if (buffer == NULL)
    {
        fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, request buffer allocation failed.");
        return ROB_ERR_OUT_OF_MEMORY;
    }

    // First, tell the peer that we are going to send it a fragmented message
    // This is a message saying just that
    fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
    if (frag_msg == NULL)
    {
        fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, fragmented message allocation failed.");
        rc = ROB_ERR_OUT_OF_MEMORY;
        goto finish_buffer_only;
    }
    memset(frag_msg, 0, sizeof(fragmented_message_t));
    frag_msg->peer = peer;
    frag_msg->media_type = media_type;
    frag_msg->is_receiver = false;
    frag_msg->last_requested = FRAG_NO_REQUESTED_FRAGMENT;
    frag_msg->send_data_length = data_length;
    frag_msg->send_data = data;
    frag_msg->fragment_count = fragment_count;
    frag_msg->fragment_size = fragment_size;
    frag_msg->hash = robusto_crc32(0, data, data_length);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) {
        if (halow_transfer_counter == UINT32_MAX) {
            ROB_LOGE(fragmentation_log_prefix, "HaLow transfer counter exhausted; restart required");
            robusto_free(frag_msg);
            rc = ROB_ERR_INVALID_ARG;
            goto finish_buffer_only;
        }
        if (halow_transfer_counter == 0) {
#ifdef USE_ESPIDF
            halow_transfer_nonce = esp_random();
#else
            halow_transfer_nonce = (uint32_t)r_micros();
#endif
        }
        frag_msg->transfer_id = ((uint64_t)halow_transfer_nonce << 32) | ++halow_transfer_counter;
    }
#endif
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    if (frag_msg->received_fragments == NULL)
    {
        fragment_stats_add(&fragment_stats.fragment_oom, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, received-fragment map allocation failed.");
        rc = ROB_ERR_OUT_OF_MEMORY;
        robusto_free(frag_msg);
        goto finish_buffer_only;
    }
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);
    frag_msg->abort_transmission = false;
    frag_msg->state = ROB_ST_RUNNING;
    frag_msg->start_time = (uint32_t)r_millis();
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (media_type == robusto_mt_halow) {
        rc = halow_feedback_begin(frag_msg);
        if (rc != ROB_OK) {
            robusto_free(frag_msg->received_fragments);
            robusto_free(frag_msg);
            goto finish_buffer_only;
        }
    } else
#endif
    {
        SLIST_INSERT_HEAD(&fragmented_messages_head, frag_msg, fragmented_messages);
        last_frag_msg = frag_msg;
    }

    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_REQUEST;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &data_length, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 6, &fragment_count, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 10, &frag_msg->fragment_size, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 14, &frag_msg->hash, 4);

    uint32_t msg_hash = robusto_crc32(0, buffer + 4, 18);
    memcpy(buffer, &msg_hash, 4);

    ROB_LOGD(fragmentation_log_prefix, "Sending a fragment request:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 18);

    rob_ret_val_t request_rc = send_fragment_packet(
        peer, media_type, frag_msg, buffer, ROBUSTO_CRC_LENGTH + 18, true, send_message);
    if (request_rc != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a failure sending");
        rc = media_type == robusto_mt_halow ? request_rc : ROB_FAIL;
        if (media_type == robusto_mt_halow && request_rc == ROB_ERR_TIMEOUT) {
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        }
        goto finish;
    }

    send_fragments(peer, media_type, frag_msg, send_message);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (media_type == robusto_mt_halow) robusto_halow_dump_tx_trace();
#endif
    ROB_LOGD(fragmentation_log_prefix, "Waiting for fragmented message to complete, current state: %u, start time: %lu", frag_msg->state, frag_msg->start_time);
    
    uint32_t starttime;
    uint32_t halow_status_start = (uint32_t)r_millis();
    // A state machine that handles the probes
    while (1)
    {
        if ((uint32_t)(r_millis() - frag_msg->start_time) > FRAG_SEND_TOTAL_WAIT_MS)
        {
            ROB_LOGE(fragmentation_log_prefix,
                     "Fragmented message exceeded send timeout (%lu ms), closing the transmission. peer=%s mt=%u bytes=%lu fragments=%lu state=%u rssi_valid=%u rssi_dbm=%i",
                     (uint32_t)FRAG_SEND_TOTAL_WAIT_MS,
                     peer->name,
                     (unsigned)media_type,
                     data_length,
                     fragment_count,
                     frag_msg->state,
                     media != NULL && media->latest_rssi_valid ? 1U : 0U,
                     media != NULL ? (int)media->latest_rssi_dbm : 0);
            frag_msg->abort_transmission = true;
            rc = ROB_ERR_TIMEOUT;
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
        }

        if (media_type != robusto_mt_halow && frag_msg->state == ROB_ST_RETRYING &&
            frag_msg->retry_start_time != 0U &&
            (uint32_t)(r_millis() - frag_msg->retry_start_time) > FRAG_RETRY_TOTAL_WAIT_MS)
        {
            ROB_LOGE(fragmentation_log_prefix,
                     "Fragmented message exceeded retry timeout (%lu ms), closing the transmission. peer=%s mt=%u bytes=%lu fragments=%lu state=%u rssi_valid=%u rssi_dbm=%i",
                     (uint32_t)FRAG_RETRY_TOTAL_WAIT_MS,
                     peer->name,
                     (unsigned)media_type,
                     data_length,
                     fragment_count,
                     frag_msg->state,
                     media != NULL && media->latest_rssi_valid ? 1U : 0U,
                     media != NULL ? (int)media->latest_rssi_dbm : 0);
            frag_msg->abort_transmission = true;
            rc = ROB_ERR_TIMEOUT;
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
        }

#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
        if (media_type == robusto_mt_halow) {
            if (!halow_consume_feedback(frag_msg)) {
                rc = ROB_ERR_MUTEX;
                goto finish;
            }
            if (frag_msg->state == ROB_ST_RETRYING) {
                send_fragments(peer, media_type, frag_msg, send_message);
                halow_status_start = (uint32_t)r_millis();
                continue;
            }
            if (frag_msg->state == ROB_ST_DONE || frag_msg->state == ROB_ST_PAUSED) {
                if ((uint32_t)(r_millis() - halow_status_start) >= FRAG_RESULT_WAIT_MS) {
                    frag_msg->state = ROB_ST_PAUSED;
                    rc = send_frag_check(peer, media_type, frag_msg, send_message);
                    if (rc != ROB_OK) {
                        if (rc == ROB_ERR_TIMEOUT) {
                            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
                        }
                        goto finish;
                    }
                    halow_status_start = (uint32_t)r_millis();
                }
                send_wait_yield(media_type);
                continue;
            }
        }
#else
        (void)halow_status_start;
#endif
        starttime = r_millis();
        uint32_t wait_ms = frag_msg->state == ROB_ST_PAUSED ? FRAG_STATUS_WAIT_MS : FRAG_RUNNING_WAIT_MS;
        // First, we identify that we are at least done.
        while (frag_msg->state < ROB_ST_DONE && (r_millis() < starttime + wait_ms))
        {
            send_wait_yield(media_type);
        }
        if (r_millis() > (starttime + wait_ms))
        {
            ROB_LOGE(fragmentation_log_prefix, "Waited for fragmented message state %u too long (%lu ms), timing out and closing the transmission.", frag_msg->state, wait_ms);
            frag_msg->abort_transmission = true;
            rc = ROB_ERR_TIMEOUT;
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
        }

        if (frag_msg->state == ROB_ST_DONE)
        {
            // If we are in done state, await result or timeout //TODO: ESP need about 500, BLE 2000, be media-specific?
            starttime = r_millis();
            while ((frag_msg->state == ROB_ST_DONE) && (r_millis() < starttime + FRAG_RESULT_WAIT_MS))
            {
                send_wait_yield(media_type);
            }
            if (frag_msg->state == ROB_ST_DONE)
            {
                // We haven't received a result, ask for it.
                frag_msg->state = ROB_ST_PAUSED;
                ROB_LOGW(fragmentation_log_prefix,
                         "Fragment result pending, sending status check hash=%lu bytes=%lu fragments=%lu wait_ms=%lu",
                         (unsigned long)frag_msg->hash,
                         (unsigned long)data_length,
                         (unsigned long)fragment_count,
                         (unsigned long)FRAG_RESULT_WAIT_MS);
                rob_ret_val_t check_rc = send_frag_check(peer, media_type, frag_msg, send_message);
                if (check_rc != ROB_OK)
                {
                    ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment status check hash=%lu rc=%i", (unsigned long)frag_msg->hash, check_rc);
                    rc = check_rc;
                    goto finish;
                }
                continue;
            }
        }
        if (frag_msg->state == ROB_ST_SUCCEEDED)
        {
            ROB_LOGD(fragmentation_log_prefix,
                     "Fragmented message sent successfully hash=%lu bytes=%lu fragments=%lu",
                     (unsigned long)frag_msg->hash,
                     (unsigned long)data_length,
                     (unsigned long)fragment_count);
            rc = ROB_OK;
            fragment_stats_add(&fragment_stats.send_succeeded, 1U, ROBUSTO_STATS_LEVEL_BASIC);
            goto finish;
        }

        switch (frag_msg->state)
        {
        case ROB_ST_TIMED_OUT:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message timed out.");
            rc = ROB_FAIL;
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
            break;
        case ROB_ST_ABORTED:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message aborted.");
            rc = ROB_FAIL;
            fragment_stats_add(&fragment_stats.send_failed, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
            break;
        case ROB_ST_FAILED:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message failed; see local send or receiver result errors.");
            rc = ROB_FAIL;
            fragment_stats_add(&fragment_stats.send_failed, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
            break;
        case ROB_ST_RETRYING:
            ROB_LOGW(fragmentation_log_prefix, "Sending fragmented message is retrying.");
            continue;
            break;
        case ROB_ST_PAUSED:
            ROB_LOGD(fragmentation_log_prefix, "Sending fragmented message is waiting for fragment status.");
            continue;
            break;
        default:
            ROB_LOGE(fragmentation_log_prefix, "Internal error: Sending fragmented message ended in an unexpected state: %u.", frag_msg->state);
            rc = ROB_FAIL;
            fragment_stats_add(&fragment_stats.send_failed, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            goto finish;
            break;
        }
    }
finish:
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (media_type == robusto_mt_halow) robusto_halow_dump_tx_trace();
#endif
    if (media != NULL)
    {
        media->postpone_qos = false;
    }
    remove_fragmented_message(frag_msg);
finish_buffer_only:
    robusto_free(buffer);

    return rc;
}

void robusto_message_fragment_init(char *_log_prefix)
{
    fragmentation_log_prefix = _log_prefix;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    memset(halow_completed_feedback, 0, sizeof(halow_completed_feedback));
    halow_completed_feedback_next = 0;
    if (halow_feedback_mutex == NULL) {
        halow_feedback_mutex = robusto_mutex_init();
        if (halow_feedback_mutex == NULL) {
            ROB_LOGE(fragmentation_log_prefix, "HaLow fragment feedback mutex initialization failed");
        }
    }
#endif
    last_frag_msg = NULL;
    robusto_fragment_stats_reset();
    robusto_fragment_stats_set_level(ROBUSTO_STATS_LEVEL_VERBOSE);
    memset(completed_fragment_transfers, 0, sizeof(completed_fragment_transfers));
    SLIST_INIT(&fragmented_messages_head); /* Initialize the queue */
}
