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
#else
#define SKIP_FRAGMENT_TEST 1
#endif

static char *fragmentation_log_prefix = "NOT SET";

SLIST_HEAD(slist_fragmented_messages_head, fragmented_message);

struct slist_fragmented_messages_head fragmented_messages_head;

// Cache the last frag_msg for optimization
static fragmented_message_t *last_frag_msg = NULL;

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
    robusto_free(frag_msg);
}

fragmented_message_t *find_fragmented_message(uint32_t hash)
{

    fragmented_message_t *retval;
    if ((!last_frag_msg) || ((last_frag_msg) && (last_frag_msg->hash != hash)))
    {
        if (!last_frag_msg)
        {
            return NULL;
        }

        // TODO: Implement finding message hashes
        ROB_LOGE(fragmentation_log_prefix, "Time to implement looking up the fragment message, existing hash was %" PRIu32 "", last_frag_msg->hash);
        retval = NULL;
    }
    else
    {
        ROB_LOGD(fragmentation_log_prefix, "Matched with cached frag");
        retval = last_frag_msg;
    }
    if (retval)
    {
        return retval->abort_transmission ? NULL : retval;
    }
    else
    {
        return NULL;
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

    if (*(uint32_t *)(data) != robusto_crc32(0, data + 4, 18))
    {
        add_to_history(media, false, ROB_FAIL);
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because hash mismatch.");
        return;
    }

    if (len < ROBUSTO_CRC_LENGTH + 18)
    {
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because wrong length, %i.", len);
    }
    uint32_t hash;
    memcpy(&hash, data + ROBUSTO_CRC_LENGTH + 14, 4);

    fragmented_message_t *frag_msg = find_fragmented_message(hash);
    if (!frag_msg)
    {
        frag_msg = robusto_malloc(sizeof(fragmented_message_t));
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
    ROB_LOGI(fragmentation_log_prefix, "Fragmented initialization received, info:\n \
        data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
             frag_msg->receive_buffer_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);
    /* When the frag_msg was created, a non-used element */
    frag_msg->start_time = r_millis();

    last_frag_msg = frag_msg;
    media->last_receive = r_millis();
}

void check_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message)
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
        // Gather missing fragments into an array
        ROB_LOGI(fragmentation_log_prefix, "We have %lu missing fragment(s).", missing_fragments);
        uint8_t *missing = robusto_malloc(ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        memcpy(missing, &frag_msg->hash, 4);
        missing[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
        missing[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESEND;

        for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
        {
            missing[ROBUSTO_CRC_LENGTH + 2 + missing_counter] = frag_msg->received_fragments[missing_counter];
        }
        rob_log_bit_mesh(ROB_LOG_DEBUG, fragmentation_log_prefix, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        send_message(peer, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count, false);
        return;
    }
    else
    {
        // Last, check hash and
        if (frag_msg->hash != robusto_crc32(0, frag_msg->receive_buffer, frag_msg->receive_buffer_length))
        {
            ROB_LOGE(fragmentation_log_prefix, "The full message did not match with the hash");
            send_result(peer, frag_msg, ROB_ERR_WRONG_CRC, send_message);
            // TODO: Send check result failed. We have no way of knowing which part failed.
            return;
        }
        else
        {
            ROB_LOGI(fragmentation_log_prefix, "The assembled %lu-byte multimessage matched the hash, passing to incoming.", frag_msg->receive_buffer_length);
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
    ROB_LOGD(fragmentation_log_prefix, "handle_frag_message (hash %lu)", *(uint32_t *)data);
    robusto_media_t *media = get_media_info(peer, media_type);

#if ROB_LOG_LOCAL_LEVEL > ROB_LOG_INFO
#warning "Sending fragmented messages may fail if debug logging is enabled."
#endif

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
        ROB_LOGW(fragmentation_log_prefix, "Invalid fragment reference.");
        return;
    }

    uint32_t msg_frag_count;
    memcpy(&msg_frag_count, data + ROBUSTO_CRC_LENGTH + 2, 4);

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
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of fragment %lu: %i bytes, expected %lu", msg_frag_count, len, expected_message_length);
        return;
    }

    // Length of the data checks out
    ROB_LOGD(fragmentation_log_prefix, "Storing fragment: %lu. Offset: %lu, length: %i", msg_frag_count, fragment_size * msg_frag_count, len - FRAG_HEADER_LEN);
    memcpy(frag_msg->receive_buffer + (frag_msg->fragment_size * msg_frag_count), data + FRAG_HEADER_LEN, len - FRAG_HEADER_LEN);
    frag_msg->received_fragments[msg_frag_count] = 1;

    // Are we at the last, or last requested, fragment, no less?
    if ((frag_msg->last_requested > 0 && msg_frag_count == frag_msg->last_requested) ||
        (msg_frag_count == frag_msg->fragment_count - 1))
    {
        ROB_LOGI(fragmentation_log_prefix, "We are on the last fragment (initial send or re-send)");
        // If received fragments doesn't add up to fragment count, send list of missing fragments to sender
        // Note that w  e probably do not want to handle larger data typically, even though SPIRAM may support it. A larger ESP-NOW frame size would change that though.

        check_fragments(peer, media_type, frag_msg, send_message);
    }
    ROB_LOGD(fragmentation_log_prefix, "Returning from handle_frag_message");
}

void send_fragments(robusto_peer_t *peer, e_media_type media_type, fragmented_message_t *frag_msg, cb_send_message *send_message)
{

    ROB_LOGI(fragmentation_log_prefix, "Sending %" PRIu32 " fragments:", frag_msg->fragment_count);
    // rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, data_length);

    // TODO: Apparently, ESP-NOW has no acknowledgement, we might need to add the same we do for LoRa, for example.

    uint8_t *buffer = NULL;
    uint32_t curr_frag_size = curr_frag_size = frag_msg->fragment_size;

    robusto_media_t *info = get_media_info(peer, media_type);
    for (uint32_t curr_fragment = 0; curr_fragment < frag_msg->fragment_count; curr_fragment++)
    {
        if (frag_msg->abort_transmission)
        {
            return;
        }
        // QoS will disturb sending like this
        info->postpone_qos = true;

        buffer = robusto_malloc(frag_msg->fragment_size + FRAG_HEADER_LEN);
        // We always send the same hash, as an identifier
        memcpy(buffer, &frag_msg->hash, 4);
        buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
        buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_MESSAGE;
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
            ROB_LOGI(fragmentation_log_prefix, "Re-sending fragment %lu (of %lu), pos %lu, length %lu bytes of (%lu total bytes).",
                     curr_fragment + 1, frag_msg->fragment_count, frag_msg->fragment_size * curr_fragment, curr_frag_size, frag_msg->send_data_length);
        }
        else
        {
            ROB_LOGD(fragmentation_log_prefix, "Sending fragment %lu (of %lu), pos %lu, length %lu bytes of (%lu total bytes).",
                     curr_fragment + 1, frag_msg->fragment_count, frag_msg->fragment_size * curr_fragment, curr_frag_size, frag_msg->send_data_length);
        }
        memcpy(buffer + FRAG_HEADER_LEN, frag_msg->send_data + (frag_msg->fragment_size * curr_fragment), curr_frag_size);

        if (SKIP_FRAGMENT_TEST)
        {
            if (send_message(peer, buffer, FRAG_HEADER_LEN + curr_frag_size, false) != ROB_OK)
            {
                ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment [%" PRIu32 "].", curr_fragment);
            }
        }

        robusto_yield();
    }
    frag_msg->state = ROB_ST_DONE;
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
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_resend");
    robusto_media_t *media = get_media_info(peer, media_type);

    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_resend, fragment count: %lu ", frag_msg->fragment_count);
    frag_msg->state = ROB_ST_RETRYING;
    if (len - (ROBUSTO_CRC_LENGTH + 2) != frag_msg->fragment_count)
    {
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of missing fragments data: %i. Expected: %lu.", len - (ROBUSTO_CRC_LENGTH + 2), frag_msg->fragment_count);
        media->receive_failures++;
        return;
    }
    media->receive_successes++;
    frag_msg->state = ROB_ST_RETRYING;
    memcpy(frag_msg->received_fragments, data + ROBUSTO_CRC_LENGTH + 2, len - (ROBUSTO_CRC_LENGTH + 2));
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
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_check");
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }
    else
    {
        check_fragments(peer, media_type, frag_msg, send_message);
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

void handle_frag_result(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_result");
    rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, len);
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }
    int16_t result;
    memcpy(&result, data + ROBUSTO_CRC_LENGTH + 2, 2);

    ROB_LOGI(fragmentation_log_prefix, "result: %" PRIi16, result);
    switch (result)
    {
    case ROB_OK:
        ROB_LOGI(fragmentation_log_prefix, "Receiver reports successful transmission.");
        frag_msg->state = ROB_ST_SUCCEEDED;
        break;
    case ROB_FAIL:
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports unsuccessful successful transmission.");
        frag_msg->state = ROB_ST_FAILED;
        break;
    case ROB_ERR_WRONG_CRC:
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports CRC mismatch");
        frag_msg->state = ROB_ST_FAILED;
        break;
    case ROB_ERR_TIMEOUT:
        ROB_LOGE(fragmentation_log_prefix, "Receiver reports timeout ");
        frag_msg->state = ROB_ST_TIMED_OUT;
        break;
    default:
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
bool handle_fragmented(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
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

    ROB_LOGI(fragmentation_log_prefix, "In send_frag_check");
    uint8_t *msg_frag_check = robusto_malloc(ROBUSTO_CRC_LENGTH + 2);
    memcpy(msg_frag_check, &frag_msg->hash, 4);
    msg_frag_check[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    msg_frag_check[ROBUSTO_CRC_LENGTH + 1] = FRAG_CHECK;

    send_message(peer, msg_frag_check, ROBUSTO_CRC_LENGTH + 2, false);
    // TODO: We should probably free data here as well. And duplicate the data in the test send_message call back.
    return ROB_OK;
}
/**
 * @brief Sends a fragmented message
 */
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, uint32_t fragment_size, cb_send_message *send_message)
{
    int rc = ROB_FAIL;
    // How many parts will it have?
    uint32_t fragment_count = data_length / fragment_size;
    if (fragment_count % fragment_size > 0)
    {
        fragment_count++;
    }

    ROB_LOGI(fragmentation_log_prefix, "Creating and sending a %lu-part, %lu-byte fragmented message in %lu-byte chunks.", fragment_count, data_length, fragment_size);
    //   rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, data_length);
    uint32_t curr_part = 0;
    uint8_t *buffer = robusto_malloc(fragment_size + sizeof(curr_part));

    // First, tell the peer that we are going to send it a fragmented message
    // This is a message saying just that
    fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
    frag_msg->send_data_length = data_length;
    frag_msg->send_data = data;
    frag_msg->fragment_count = fragment_count;
    frag_msg->fragment_size = fragment_size;
    frag_msg->hash = robusto_crc32(0, data, data_length);
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);
    frag_msg->abort_transmission = false;
    frag_msg->state = ROB_ST_RUNNING;
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

    ROB_LOGI(fragmentation_log_prefix, "Sending a fragment request:");
    rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 18);

    if (send_message(peer, buffer, ROBUSTO_CRC_LENGTH + 18, true) != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a failure sending");
        rc = ROB_FAIL;
        goto finish;
    }

    send_fragments(peer, media_type, frag_msg, send_message);
    uint32_t starttime;
    // A state machine that handles the probes
    while (1)
    {
        starttime = r_millis();
        // First, we identify that we are at least done.
        while (frag_msg->state < ROB_ST_DONE && (r_millis() < starttime + 30000))
        {
            robusto_yield();
        }
        if (r_millis() > (starttime + 30000))
        {
            ROB_LOGE(fragmentation_log_prefix, "Waited for fragmented message too long, timing out and closing the transmission.");
            frag_msg->abort_transmission = true;
            r_delay(1000);
            goto finish;
        }

        if (frag_msg->state == ROB_ST_DONE)
        {
            // If we are in done state, await result or timeout //TODO: ESP need about 500, BLE 2000, be media-specific?
            starttime = r_millis();
            while ((frag_msg->state == ROB_ST_DONE) && (r_millis() < starttime + 2000))
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
            ROB_LOGI(fragmentation_log_prefix, "Fragmented message sent successfully");
            rc = ROB_OK;
            goto finish;
        }

        switch (frag_msg->state)
        {
        case ROB_ST_TIMED_OUT:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message timed out.");
            rc = ROB_FAIL;
            goto finish;
            break;
        case ROB_ST_ABORTED:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message aborted.");
            rc = ROB_FAIL;
            goto finish;
            break;
        case ROB_ST_FAILED:
            ROB_LOGE(fragmentation_log_prefix, "Sending fragmented message failed (likely bad CRC).");
            rc = ROB_FAIL;
            goto finish;
            break;
        case ROB_ST_RETRYING:
            ROB_LOGI(fragmentation_log_prefix, "Sending fragmented message is retrying.");
            continue;
            break;
        default:
            ROB_LOGE(fragmentation_log_prefix, "Internal error: Sending fragmented message ended in an unexpected state: %u.", frag_msg->state);
            rc = ROB_FAIL;
            goto finish;
            break;
        }
    }
finish:
    remove_fragmented_message(frag_msg);
    robusto_free(buffer);

    return rc;
}

void robusto_message_fragment_init(char *_log_prefix)
{
    fragmentation_log_prefix = _log_prefix;
    last_frag_msg = NULL;
    SLIST_INIT(&fragmented_messages_head); /* Initialize the queue */
}
