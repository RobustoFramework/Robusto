/**
 * @file i2c_messaging.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief I2C messaging implementation
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
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

#include "i2c_messaging.h"
#if defined(CONFIG_ROBUSTO_SUPPORTS_I2C)
#include <robusto_logging.h>
#include <robusto_time.h>
#include <robusto_peer.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include "i2c_peer.h"
#include <string.h>
#include <inttypes.h>


#if !(defined(USE_ESPIDF) && defined(USE_ARDUINO))
#include "stddef.h"
#endif


/* The log prefix for all logging */
static char *i2c_messaging_log_prefix;

void i2c_handle_incoming(uint8_t * data, uint32_t data_length){
    robusto_peer_t *peer = robusto_peers_find_peer_by_i2c_address(data[0]);
    if (peer != NULL)
    {
        ROB_LOGI(i2c_messaging_log_prefix, "<< i2c_incoming_cb got a message from a peer, data:");
        rob_log_bit_mesh(ROB_LOG_DEBUG, i2c_messaging_log_prefix, data, data_length);
    }
    else
    {
        /* We didn't find the peer */
        ROB_LOGW(i2c_messaging_log_prefix, "<< i2c_incoming_cb got a message from an unknown peer. Data:");
        rob_log_bit_mesh(ROB_LOG_WARN, i2c_messaging_log_prefix, data, data_length);
        ROB_LOGW(i2c_messaging_log_prefix, "<< Mac address:");
        rob_log_bit_mesh(ROB_LOG_WARN, i2c_messaging_log_prefix, data + 1, ROBUSTO_MAC_ADDR_LEN);
        if (data[ROBUSTO_CRC_LENGTH + 1] != 0x42)
        {
            // If it is unknown, and not a presentation, disregard
            // TODO: Marking as a presentation sort of bypasses this, is this proper?
            return;
        }

        peer = robusto_add_init_new_peer_i2c(NULL, data[0]);
    }

    bool is_heartbeat = data[ROBUSTO_CRC_LENGTH + 1] == HEARTBEAT_CONTEXT;
    if (is_heartbeat)
    {
        ROB_LOGI(i2c_messaging_log_prefix, "I2C is heartbeat");
        rob_log_bit_mesh(ROB_LOG_DEBUG, i2c_messaging_log_prefix, data + 1, data_length);
        peer->i2c_info.last_peer_receive = parse_heartbeat(data + 1, ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
    }

    if ((data[ROBUSTO_CRC_LENGTH + 1] & MSG_FRAGMENTED) == MSG_FRAGMENTED)
    {
        uint8_t *n_data = (uint8_t *)robusto_malloc(data_length -1);
        memcpy(n_data, data +1 , data_length - 1);
        handle_fragmented(peer, robusto_mt_i2c, n_data, data_length - 1, I2C_FRAGMENT_SIZE, &i2c_send_message);
        robusto_free(data);
        return;
    }

    peer->i2c_info.last_receive = r_millis();
    
    if (is_heartbeat)
    {
        add_to_history(&peer->i2c_info, false, ROB_OK);
    }
    else
    {
        uint8_t *n_data = (uint8_t *)robusto_malloc(data_length);
        memcpy(n_data, data, data_length);
        ROB_LOGI(i2c_messaging_log_prefix, "Got a message");
        add_to_history(&peer->i2c_info, false, robusto_handle_incoming(n_data, data_length - 1, peer, robusto_mt_i2c, 1));
        //robusto_free(data);
    }
}



void i2c_do_on_poll_cb(queue_context_t *q_context)
{

    uint8_t *rcv_data = NULL;
    robusto_peer_t *peer = NULL;
    uint8_t prefix_bytes = NULL;
    i2c_read_data(&rcv_data, &peer, &prefix_bytes);


}

void i2c_do_on_work_cb(media_queue_item_t *queue_item)
{
    ROB_LOGD(i2c_messaging_log_prefix, ">> In i2c work callback.");
    send_work_item(queue_item, &(queue_item->peer->i2c_info), robusto_mt_i2c, &i2c_send_message, &i2c_do_on_poll_cb, i2c_get_queue_context());
}


void i2c_messaging_init(char *_log_prefix)
{
    i2c_messaging_log_prefix = _log_prefix;
    i2c_compat_messaging_init(i2c_messaging_log_prefix);
}

#endif