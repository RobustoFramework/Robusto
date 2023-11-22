/**
 * @file robusto_message_sending.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Robusto messaging functionality
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

#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_time.h>

#ifdef USE_ESPIDF
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include <network/src/media/i2c/i2c_messaging.h>
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <network/src/media/lora/lora_messaging.hpp>
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include <network/src/media/espnow/espnow_messaging.h>
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "../media/i2c/i2c_messaging.h"
#endif
#endif // Include differently for ESP-IDF
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "../media/mock/mock_messaging.h"
#include "../media/mock/mock_queue.h"
#endif
#include <robusto_qos.h>

#include <inttypes.h>
#include <string.h>

char *message_sending_log_prefix;

/**
 * @brief  Calculate a reasonable wait time in milliseconds
 * based on I2C frequency and expected response timeout
 * Note that logging may cause this to be too short
 * @param data_length Length of data to be transmitted
 * @return int The timeout needed.
 */
/*
int calc_timeout_ms(media_definition_t *media_def, uint32_t data_length)
{
    int timeout_ms = (data_length / media_def->bytes_per_sec * 1000) + media_def->ack_timeout;
    ROB_LOGI(media_def->log_prefix, "Timeout calculated to %i ms.", timeout_ms);
    return timeout_ms;
}
*/

int get_media_type_prefix_len(e_media_type media_type, robusto_peer_t *peer)
{
    int prefix_length = 0;
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    // I2C only uses peripheral address and adds it separately
    if (media_type == robusto_mt_i2c)
    {
        prefix_length = 0;
    }
    else
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
        // ESP-NOW has external address handling
    if (media_type == robusto_mt_espnow)
    {
        prefix_length = 0;
    }
    else
#endif
    // Others, like LoRa, uses either MAC-address (UNKNOWN) or RelationId
    if (peer->state == PEER_UNKNOWN)
    {
        prefix_length = 6;
    }
    else
    {
        prefix_length = 4;
    }
    return prefix_length;
}


rob_ret_val_t send_message_raw_internal(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, int data_length, queue_state *state, bool receipt, bool heartbeat, uint8_t depth, uint8_t exclude_media_types)
{
    
    rob_ret_val_t retval = ROB_FAIL;
    queue_context_t * queue_ctx = NULL;
    
    // Put it on the right queue
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    if (media_type == robusto_mt_lora)
    {
        ROB_LOGI(message_sending_log_prefix, "Sending using LoRa..");
        queue_ctx = lora_get_queue_context();
        //retval = lora_safe_add_work_queue(peer, data, data_length, state, heartbeat, receipt);
    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    if (media_type == robusto_mt_espnow)
    {
        // TODO: Make all log messages reflect direction using >> or << where applicable
        ROB_LOGI(message_sending_log_prefix, ">> Sending using espnow...");
        queue_ctx = espnow_get_queue_context();
        //retval = espnow_safe_add_work_queue(peer, data, data_length, state, heartbeat, receipt);
    }

#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    if (media_type == robusto_mt_i2c)
    {
        ROB_LOGI(message_sending_log_prefix, ">> Sending using I2C...");
        queue_ctx = i2c_get_queue_context();
        //retval = i2c_safe_add_work_queue(peer, data, data_length, state, heartbeat, receipt);
    }

#endif

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

    if (media_type == robusto_mt_mock)
    {
        ROB_LOGI(message_sending_log_prefix, ">> Mock sending to mock host: %lu ", peer->relation_id_outgoing);
        // ROB_LOGI(message_sending_log_prefix, ">> Data %i bytes (including 4 bytes preamble): ", data_length);
        rob_log_bit_mesh(ROB_LOG_INFO, message_sending_log_prefix, data, data_length);
        queue_ctx = mock_get_queue_context();
    }
#endif
        
    if (queue_ctx != NULL) {
        media_queue_item_t *new_item = robusto_malloc(sizeof(media_queue_item_t)); 
            new_item->peer = peer;
            new_item->data = data;
            new_item->data_length = data_length;
            new_item->heartbeat = heartbeat;
            new_item->exclude_media = exclude_media_types;
            new_item->depth = depth+ 1;
            new_item->receipt = receipt;
            new_item->state = state;
        retval = robusto_set_queue_state_queued_on_ok(new_item->state, safe_add_work_queue(queue_ctx, new_item));
    } else {
        ROB_LOGE(message_sending_log_prefix, "Internal error: send_message_raw_internal for peer %s called with invalid media type: %u.", peer->name, media_type);
        return ROB_FAIL;
    }

    if (retval != ROB_OK)
    {
        robusto_media_t *info = get_media_info(peer, media_type);
        set_state(peer, info, media_type, media_state_problem, media_problem_send_problem);
        info->send_failures++;
        // If we get a problem here, there might be an internal issue, it is immidiately considered a problem.
        ROB_LOGE(message_sending_log_prefix, "Failed sending to %s, mt %hhu, res %hi, ", peer->name, media_type, retval);
    }



    return retval;
}

rob_ret_val_t send_message_raw(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, queue_state *state, bool receipt) {
    return send_message_raw_internal(peer, media_type, data, data_length, state, receipt, false, 0, robusto_mt_none);
}


rob_ret_val_t send_message_multi(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                            uint8_t *strings_data, uint32_t strings_length, uint8_t *binary_data, uint32_t binary_length, queue_state *state, e_media_type force_media_type)
{
    if (peer == NULL)
    {
        ROB_LOGE(message_sending_log_prefix, "The peer is not set!");
        return ROB_FAIL;
    }
    uint8_t *dest_message;
    uint32_t message_length = robusto_make_multi_message(MSG_MESSAGE, service_id, conversation_id,
                                                        strings_data, strings_length, binary_data, binary_length, &dest_message);
    e_media_type media_type;
    if (force_media_type == robusto_mt_none) {
        rob_ret_val_t suitability_res = set_suitable_media(peer, strings_length + binary_length, robusto_mt_none, &media_type);
        if (suitability_res != ROB_OK) {
            ROB_LOGW(message_sending_log_prefix, "set_suitable_media failed, media will not change.");
            goto fail;
        }
    } else {
        media_type = force_media_type;
    }
     
    if (send_message_raw(peer, media_type, dest_message, message_length, state, true) != ROB_OK)
    {
        goto fail;
    }
    else
    {
        return ROB_OK;
    }
fail: 
    robusto_free(dest_message);
    robusto_set_queue_state_queued_on_ok(state, ROB_FAIL);
    return ROB_FAIL;

}
rob_ret_val_t send_message_strings(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                              uint8_t *strings_data, uint32_t strings_length, queue_state *state)
{
    return send_message_multi(peer, service_id, conversation_id, strings_data, strings_length, NULL, 0, state, robusto_mt_none);
}

rob_ret_val_t send_message_binary(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                             uint8_t *binary_data, uint32_t binary_length, queue_state *state)
{
    return send_message_multi(peer, service_id, conversation_id, NULL, 0, binary_data, binary_length, state, robusto_mt_none);
}



void send_work_item(media_queue_item_t * queue_item, robusto_media_t *info, e_media_type media_type, send_callback_cb *send_callback, poll_callback_cb *poll_callback, queue_context_t *queue_context) {
    
    int retval = ROB_FAIL;
    int send_retries = 0;
    robusto_set_queue_state_running(queue_item->state);
    do
    {
        retval = send_callback(queue_item->peer, queue_item->data, queue_item->data_length, queue_item->receipt);
        if ((retval != ROB_OK) && (send_retries < 3))
        {
            ROB_LOGI(message_sending_log_prefix, ">> Retry %i failed.", send_retries + 1);
            // Call the poll function do not spam the network
            poll_callback(queue_context);
            robusto_yield();
        }

        send_retries++;

    } while ((!queue_item->heartbeat) && // We don't retry with heart beats..
    (retval != ROB_ERR_WHO) && // ..or if the peer says we are unknown
    (retval != ROB_OK) && // ..and only if the attempt failed
    (send_retries < 3)); // ..a limited number of times 
    // TODO: Handle retry count?

    add_to_history(info, true, retval);
    
    if ((retval != ROB_OK) && // We only try other medias if we have failed..
    (!queue_item->heartbeat) && // ..and if it wasn't a heartbeat
    (retval != ROB_ERR_WHO)) // ..or if the peer knew who we were
    {

        e_media_type next_media_type;
        // Add current media type to excluded media
        queue_item->exclude_media = queue_item->exclude_media | media_type;
        ROB_LOGW(message_sending_log_prefix, "Failed sending using %s, will try some other media.", media_type_to_str(media_type));
        // Find another media to try
        rob_ret_val_t suitability_res = set_suitable_media(queue_item->peer, queue_item->data_length, queue_item->exclude_media, &next_media_type);
        if (suitability_res != ROB_OK)
        {
            ROB_LOGW(message_sending_log_prefix, "Couldn't find another media to try.");
            robusto_set_queue_state_result(queue_item->state, QUEUE_STATE_FAILED);
            robusto_free(queue_item->data);
        }
        else
        {
            ROB_LOGW(message_sending_log_prefix, "Failed sending using %s, will try sending using %s instead.", media_type_to_str(media_type), media_type_to_str(next_media_type));
            // Another media found, try sending using that instead
            queue_state *new_state = (uint8_t *)robusto_malloc(sizeof(queue_state));
            robusto_set_queue_state_trying(queue_item->state);
            rob_ret_val_t retry_retval;
            rob_ret_val_t retry_res = send_message_raw_internal(queue_item->peer, next_media_type, queue_item->data, queue_item->data_length, new_state, false, queue_item->receipt, queue_item->depth, queue_item->exclude_media);
            if (retry_res != ROB_OK)
            {
                ROB_LOGE(message_sending_log_prefix, "Error queueing retry: %i %i", retry_res, next_media_type);
                robusto_set_queue_state_result(queue_item->state, ROB_FAIL);
                robusto_free(queue_item->data);
            }
            else if (!robusto_waitfor_queue_state(new_state, 6000, &retry_retval))
            {
                ROB_LOGE(message_sending_log_prefix, "Failed retrying sending presentation to %s, using mt %hhu, queue state %hhu , reason code: %hi",
                         queue_item->peer->name, next_media_type, *(uint8_t *)new_state[0], retry_retval);
                robusto_set_queue_state_result(queue_item->state, retry_retval);
                robusto_free(queue_item->data);
            }
            else
            {
                robusto_set_queue_state_result(queue_item->state, ROB_OK);
                
            }
        }

    }
    else
    {
        robusto_set_queue_state_result(queue_item->state, retval);
        robusto_free(queue_item->data); // Not if re-sent, that would re-free..
    }
    robusto_free(queue_item);
    robusto_yield();
}

void robusto_message_sending_init(char *_log_prefix)
{
    message_sending_log_prefix = _log_prefix;
}
