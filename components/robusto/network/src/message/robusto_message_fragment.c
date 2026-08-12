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
#ifdef USE_ESPIDF
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

#define FRAG_RUNNING_WAIT_MS 30000
#define FRAG_RESULT_WAIT_MS (CONFIG_ROB_RECEIPT_TIMEOUT_MS * 20)
#define FRAG_STATUS_WAIT_MS (CONFIG_ROB_RECEIPT_TIMEOUT_MS * 20)
#define FRAG_TOTAL_WAIT_MS (FRAG_RUNNING_WAIT_MS + FRAG_RESULT_WAIT_MS + FRAG_STATUS_WAIT_MS)
#define FRAG_NO_REQUESTED_FRAGMENT UINT32_MAX

static char *fragmentation_log_prefix = "NOT SET";

SLIST_HEAD(slist_fragmented_messages_head, fragmented_message);

struct slist_fragmented_messages_head fragmented_messages_head;

// Cache the last frag_msg for optimization
static fragmented_message_t *last_frag_msg = NULL;
static robusto_stats_level_t fragment_stats_level = ROBUSTO_STATS_LEVEL_VERBOSE;
static robusto_fragment_stats_t fragment_stats = {0};
static robusto_fragment_stats_t fragment_stats_last_read = {0};

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

    SLIST_REMOVE(&fragmented_messages_head, frag_msg, fragmented_message, fragmented_messages);
    if (last_frag_msg == frag_msg)
    {
        last_frag_msg = NULL;
    }
    robusto_free(frag_msg->received_fragments);
    robusto_free(frag_msg);
}

fragmented_message_t *find_fragmented_message(uint32_t hash)
{
    if (last_frag_msg && last_frag_msg->hash == hash)
    {
        ROB_LOGD(fragmentation_log_prefix, "Matched with cached frag");
        return last_frag_msg->abort_transmission ? NULL : last_frag_msg;
    }

    fragmented_message_t *curr = NULL;
    SLIST_FOREACH(curr, &fragmented_messages_head, fragmented_messages)
    {
        if (curr->hash == hash)
        {
            last_frag_msg = curr;
            return curr->abort_transmission ? NULL : curr;
        }
    }

    return NULL;
}
/**
 * @brief Send the result of a fragmentation message transmission
 *
 * @param peer
 * @param frag_msg
 * @param return_value
 * @param send_message
 */
void send_result(robusto_peer_t *peer, fragmented_message_t *frag_msg, rob_ret_val_t return_value, cb_send_message *send_message)
{
    uint8_t *buffer = robusto_malloc(ROBUSTO_CRC_LENGTH + 4);
    memcpy(buffer, &frag_msg->hash, 4);
    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESULT;

    int16_t tmp_retval = return_value;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &tmp_retval, sizeof(int16_t));
    send_message(peer, buffer, ROBUSTO_CRC_LENGTH + 2 + sizeof(int16_t), false);
    robusto_free(buffer);
}

/**
 * @brief Handle a fragmentated message request
 *
 * @param peer
 * @param data
 * @param len
 * @param fragment_size
 */
void handle_frag_request(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size)
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

    fragmented_message_t *frag_msg = find_fragmented_message(hash);
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
        frag_msg->last_requested = FRAG_NO_REQUESTED_FRAGMENT;
        SLIST_INSERT_HEAD(&fragmented_messages_head, frag_msg, fragmented_messages);
    }
    else
    {
        ROB_LOGI(fragmentation_log_prefix, "The fragment transmission is already added, assuming same properties. Might be duplicate try or or testing.");
    }
    memcpy(&frag_msg->receive_buffer_length, data + ROBUSTO_CRC_LENGTH + 2, 4);
    memcpy(&frag_msg->fragment_count, data + ROBUSTO_CRC_LENGTH + 6, 4);
    memcpy(&frag_msg->fragment_size, data + ROBUSTO_CRC_LENGTH + 10, 4);
    frag_msg->hash = hash;
    // TODO: How big should we allow before SPIRAM and more?
    frag_msg->receive_buffer = robusto_malloc(frag_msg->receive_buffer_length);
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    frag_msg->abort_transmission = false;
    frag_msg->state = ROB_ST_RUNNING;
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);
    ROB_LOGD(fragmentation_log_prefix, "Fragmented initialization received, info:\n \
        data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
             frag_msg->receive_buffer_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);
    /* When the frag_msg was created, a non-used element */
    frag_msg->start_time = (uint32_t)r_millis();

    last_frag_msg = frag_msg;
    media->last_receive = r_millis();
}

void check_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message, bool send_resend_request)
{

    // Check that we have no missing parts, we do this extra pass to save memory
    uint32_t missing_fragments = 0;

    for (uint32_t pass_one_count = 0; pass_one_count < frag_msg->fragment_count; pass_one_count++)
    {
        if (frag_msg->received_fragments[pass_one_count] == 0)
        {
            missing_fragments++;
            frag_msg->last_requested = pass_one_count;
        }
    }
    if (missing_fragments > 0)
    {
        if (!send_resend_request)
        {
            ROB_LOGI(fragmentation_log_prefix,
                     "Last fragment arrived with %lu missing fragment(s); waiting for in-flight fragments before requesting resend. first_missing=%lu last_missing=%lu hash=%lu",
                     (unsigned long)missing_fragments,
                     (unsigned long)fragment_first_missing(frag_msg),
                     (unsigned long)frag_msg->last_requested,
                     (unsigned long)frag_msg->hash);
            return;
        }
        // Gather missing fragments into an array
        fragment_stats_add(&fragment_stats.resend_request_sent, 1U, ROBUSTO_STATS_LEVEL_BASIC);
        fragment_stats_add(&fragment_stats.missing_fragments_reported, missing_fragments, ROBUSTO_STATS_LEVEL_BASIC);
        ROB_LOGW(fragmentation_log_prefix,
             "We have %lu missing fragment(s). first_missing=%lu last_missing=%lu hash=%lu",
               (unsigned long)missing_fragments,
               (unsigned long)fragment_first_missing(frag_msg),
               (unsigned long)frag_msg->last_requested,
               (unsigned long)frag_msg->hash);
        uint8_t *missing = robusto_malloc(ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        memcpy(missing, &frag_msg->hash, 4);
        missing[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
        missing[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESEND;

        for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
        {
            missing[ROBUSTO_CRC_LENGTH + 2 + missing_counter] = frag_msg->received_fragments[missing_counter];
        }
        rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        send_message(peer, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count, true);
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
            send_result(peer, frag_msg, ROB_ERR_WRONG_CRC, send_message);
            remove_fragmented_message(frag_msg);
            // TODO: Send check result failed. We have no way of knowing which part failed.
            return;
        }
        else
        {
            ROB_LOGD(fragmentation_log_prefix, "The assembled %lu-byte multimessage matched the hash, passing to incoming.", frag_msg->receive_buffer_length);
            // rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, frag_msg->receive_buffer, frag_msg->receive_buffer_length > 100 ? 100:frag_msg->receive_buffer_length);
            send_result(peer, frag_msg, ROB_OK, send_message);

            add_to_history(get_media_info(peer, media_type), false, robusto_handle_incoming(frag_msg->receive_buffer, frag_msg->receive_buffer_length, peer, media_type, 0));
            remove_fragmented_message(frag_msg);
            return;
        }
        
    }
}
void handle_frag_message(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
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
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        ROB_LOGW(fragmentation_log_prefix, "Invalid fragment reference.");
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
    memcpy(frag_msg->receive_buffer + (frag_msg->fragment_size * msg_frag_count), data + FRAG_HEADER_LEN, len - FRAG_HEADER_LEN);
    frag_msg->received_fragments[msg_frag_count] = 1;

    // Are we at the last, or last requested, fragment, no less?
    if ((frag_msg->last_requested != FRAG_NO_REQUESTED_FRAGMENT && msg_frag_count == frag_msg->last_requested) ||
        (msg_frag_count == frag_msg->fragment_count - 1))
    {
        uint32_t missing_after_store = fragment_missing_count(frag_msg);
        uint32_t first_missing = fragment_first_missing(frag_msg);

        if (missing_after_store == 0)
        {
            ROB_LOGI(fragmentation_log_prefix,
                     "Fragment boundary complete index=%lu hash=%lu state=%u",
                     (unsigned long)msg_frag_count,
                     (unsigned long)frag_msg->hash,
                     frag_msg->state);
        }
        else
        {
            ROB_LOGI(fragmentation_log_prefix,
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
        check_fragments(peer, media_type, frag_msg, send_message, is_requested_fragment);
    }
    ROB_LOGD(fragmentation_log_prefix, "Returning from handle_frag_message");
}

void send_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message)
{

    ROB_LOGD(fragmentation_log_prefix, "Sending %" PRIu32 " fragments:", frag_msg->fragment_count);
    
    uint8_t *buffer = NULL;
    uint32_t curr_frag_size = curr_frag_size = frag_msg->fragment_size;

    robusto_media_t *info = get_media_info(peer, media_type);

    // Allocate a buffer big enough for the largest fragment
    buffer = robusto_malloc(frag_msg->fragment_size + FRAG_HEADER_LEN);
    // We always send the same hash, as an identifier
    memcpy(buffer, &frag_msg->hash, 4);
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_MESSAGE;

    for (uint32_t curr_fragment = 0; curr_fragment < frag_msg->fragment_count; curr_fragment++)
    {
        if (frag_msg->abort_transmission)
        {
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
            ROB_LOGW(fragmentation_log_prefix,
                     "About to send fragment index=10 hash=%lu state=%u len=%lu total_frags=%lu total_bytes=%lu",
                     (unsigned long)frag_msg->hash,
                     frag_msg->state,
                     (unsigned long)curr_frag_size,
                     (unsigned long)frag_msg->fragment_count,
                     (unsigned long)frag_msg->send_data_length);
        }

        if (SKIP_FRAGMENT_TEST)
        {
            if (frag_msg->state == ROB_ST_RETRYING)
            {
                fragment_stats_add(&fragment_stats.fragments_resent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
            }
            else
            {
                fragment_stats_add(&fragment_stats.fragments_sent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
            }

            rob_ret_val_t send_retval = send_message(peer, buffer, FRAG_HEADER_LEN + curr_frag_size, true);
            if (curr_fragment == 10U)
            {
                ROB_LOGW(fragmentation_log_prefix,
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
                // TODO: We till need to handle failed sends
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

void handle_frag_resend(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    fragment_stats_add(&fragment_stats.resend_request_received, 1U, ROBUSTO_STATS_LEVEL_BASIC);
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_resend");
    robusto_media_t *media = get_media_info(peer, media_type);

    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_resend, fragment count: %lu ", frag_msg->fragment_count);
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

void handle_frag_check(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    fragment_stats_add(&fragment_stats.check_received, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_check");
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    else
    {
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

void handle_frag_result(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    ROB_LOGD(fragmentation_log_prefix, "In handle_frag_result");
    rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, data, len);
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        fragment_stats_add(&fragment_stats.invalid_fragment_reference, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
        return;
    }
    int16_t result;
    memcpy(&result, data + ROBUSTO_CRC_LENGTH + 2, 2);

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
        ROB_LOGE(fragmentation_log_prefix, "Unhandled fragmentation result code: %i", result);
        break;
    }
}

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
    switch (data[ROBUSTO_CRC_LENGTH + 1])
    {
    case FRAG_REQUEST:
        handle_frag_request(peer, media_type, data, len, fragment_size);
        receipt = true;
        break;
    case FRAG_MESSAGE:
        handle_frag_message(peer, media_type, data, len, fragment_size, send_message);
        break;
    case FRAG_RESEND:
        handle_frag_resend(peer, media_type, data, len, fragment_size, send_message);
        break;
    case FRAG_RESULT:
        handle_frag_result(peer, media_type, data, len, fragment_size, send_message);
        break;
    case FRAG_CHECK:
        handle_frag_check(peer, media_type, data, len, fragment_size, send_message);
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

    fragment_stats_add(&fragment_stats.check_sent, 1U, ROBUSTO_STATS_LEVEL_VERBOSE);
    ROB_LOGD(fragmentation_log_prefix, "In send_frag_check");
    uint8_t *msg_frag_check = robusto_malloc(ROBUSTO_CRC_LENGTH + 2);
    memcpy(msg_frag_check, &frag_msg->hash, 4);
    msg_frag_check[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    msg_frag_check[ROBUSTO_CRC_LENGTH + 1] = FRAG_CHECK;

    send_message(peer, msg_frag_check, ROBUSTO_CRC_LENGTH + 2, false);
    // TODO: We should probably free data here as well. And duplicate the data in the test send_message call back.
    robusto_free(msg_frag_check);
    return ROB_OK;
}
/**
 * @brief Sends a fragmented message
 */
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, uint32_t fragment_size, cb_send_message *send_message)
{
    int rc = ROB_FAIL;
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
    frag_msg->last_requested = FRAG_NO_REQUESTED_FRAGMENT;
    frag_msg->send_data_length = data_length;
    frag_msg->send_data = data;
    frag_msg->fragment_count = fragment_count;
    frag_msg->fragment_size = fragment_size;
    frag_msg->hash = robusto_crc32(0, data, data_length);
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
    SLIST_INSERT_HEAD(&fragmented_messages_head, frag_msg, fragmented_messages);

    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_REQUEST;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &data_length, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 6, &fragment_count, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 10, &frag_msg->fragment_size, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 14, &frag_msg->hash, 4);

    last_frag_msg = frag_msg;

    uint32_t msg_hash = robusto_crc32(0, buffer + 4, 18);
    memcpy(buffer, &msg_hash, 4);

    ROB_LOGD(fragmentation_log_prefix, "Sending a fragment request:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 18);

    if (send_message(peer, buffer, ROBUSTO_CRC_LENGTH + 18, true) != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a failure sending");
        rc = ROB_FAIL;
        goto finish;
    }

    send_fragments(peer, media_type, frag_msg, send_message);
    ROB_LOGD(fragmentation_log_prefix, "Waiting for fragmented message to complete, current state: %u, start time: %lu", frag_msg->state, frag_msg->start_time);
    
    uint32_t starttime;
    // A state machine that handles the probes
    while (1)
    {
        if ((uint32_t)(r_millis() - frag_msg->start_time) > FRAG_TOTAL_WAIT_MS)
        {
            robusto_media_t *media = get_media_info(peer, media_type);
            ROB_LOGE(fragmentation_log_prefix,
                     "Fragmented message exceeded total timeout (%lu ms), closing the transmission. peer=%s mt=%hhu bytes=%lu fragments=%lu state=%u rssi_valid=%u rssi_dbm=%i",
                     (uint32_t)FRAG_TOTAL_WAIT_MS,
                     peer->name,
                     media_type,
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

        starttime = r_millis();
        uint32_t wait_ms = frag_msg->state == ROB_ST_PAUSED ? FRAG_STATUS_WAIT_MS : FRAG_RUNNING_WAIT_MS;
        // First, we identify that we are at least done.
        while (frag_msg->state < ROB_ST_DONE && (r_millis() < starttime + wait_ms))
        {
            robusto_yield();
        }
        if (r_millis() > (starttime + wait_ms))
        {
            ROB_LOGE(fragmentation_log_prefix, "Waited for fragmented message state %u too long (%lu ms), timing out and closing the transmission.", frag_msg->state, wait_ms);
            frag_msg->abort_transmission = true;
            rc = ROB_ERR_TIMEOUT;
            fragment_stats_add(&fragment_stats.send_timed_out, 1U, ROBUSTO_STATS_LEVEL_ERRORS);
            r_delay(1000);
            goto finish;
        }

        if (frag_msg->state == ROB_ST_DONE)
        {
            // If we are in done state, await result or timeout //TODO: ESP need about 500, BLE 2000, be media-specific?
            starttime = r_millis();
            while ((frag_msg->state == ROB_ST_DONE) && (r_millis() < starttime + FRAG_RESULT_WAIT_MS))
            {
                robusto_yield();
            }
            if (frag_msg->state == ROB_ST_DONE)
            {
                // We haven't received a result, ask for it.
                frag_msg->state = ROB_ST_PAUSED;
                send_frag_check(peer, media_type, frag_msg, send_message);
                continue;
            }
        }
        if (frag_msg->state == ROB_ST_SUCCEEDED)
        {
            ROB_LOGD(fragmentation_log_prefix, "Fragmented message sent successfully");
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
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message failed (likely bad CRC).");
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
    remove_fragmented_message(frag_msg);
finish_buffer_only:
    robusto_free(buffer);

    return rc;
}

void robusto_message_fragment_init(char *_log_prefix)
{
    fragmentation_log_prefix = _log_prefix;
    last_frag_msg = NULL;
    robusto_fragment_stats_reset();
    robusto_fragment_stats_set_level(ROBUSTO_STATS_LEVEL_VERBOSE);
    SLIST_INIT(&fragmented_messages_head); /* Initialize the queue */
}
