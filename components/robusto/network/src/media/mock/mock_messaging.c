/**
 * @file mock_messaging.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Mock messaging
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

#include "mock_messaging.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_peer.h>
#include <robusto_incoming.h>
#include <inttypes.h>
#include <string.h>

#include "mock_queue.h"

// TODO: This is test data, question is if mock messaging should be in the components at all.
#define MOCK_STRINGS "ABC\000123\000"
#define MOCK_BINARY "\x00\x00\xFF\xFF"

char mock_strings[8] = MOCK_STRINGS;
char mock_binary[4] = MOCK_BINARY;
#define MOCK_PEERNAME_0 "PEER0\x00"
#define MOCK_PEERNAME_1 "PEER1\x00"
char mock_peername_0[6] = MOCK_PEERNAME_0;
char mock_peername_1[6] = MOCK_PEERNAME_1;

static char *mock_messaging_log_prefix;

rob_ret_val_t mock_before_comms(bool is_sending, bool first_call)
{

    return ROB_OK;
}
rob_ret_val_t mock_after_comms(bool is_sending, bool first_call)
{
    return ROB_OK;
}


rob_ret_val_t mock_read_receipt(robusto_peer_t *peer, uint8_t **dest_data)
{
    if (strcmp(peer->name, MOCK_PEERNAME_0) != 0)
    {
        ROB_LOGE("MOCK", "mock_read_receipt: The peer name it not '%s', but '%s'", MOCK_PEERNAME_0, peer->name);
    }
    char *receipt = "\xff\x00\xe4\x57\x3d\xb4";
    memcpy(*dest_data, receipt, 6);
    int ret = ROB_OK;
    return ret;
}

int mock_read_data(uint8_t **rcv_data, robusto_peer_t **peer)
{
    // Set the offset for the current message, for example, in I2C it is always one byte, the address.
    uint8_t message_expectation = get_message_expectation();
    
    if (message_expectation == 0)
    {
        return ROB_FAIL;
    }
    int length = 0;
    if ((message_expectation == MMI_STRINGS) || (message_expectation == MMI_STRINGS_ASYNC))
    {   
        length = robusto_make_strings_message(MSG_MESSAGE, 0, 0, (uint8_t *)&mock_strings, 8, rcv_data);
        *peer = robusto_peers_find_peer_by_name("TEST_MOCK");
        if ((*peer) != NULL) {
            (*peer)->mock_info.last_receive = r_millis();
        } else {
            ROB_LOGE("MOCK", "TEST_MOCK not found!");
        }

    }
    else if (message_expectation == MMI_BINARY)
    {
        length = robusto_make_binary_message(MSG_MESSAGE, 0, 0, (uint8_t *)&mock_binary, 4, rcv_data);
    }
    else if (message_expectation == MMI_SERVICE)
    {
        length = robusto_make_binary_message(MSG_MESSAGE, 1959, 0, (uint8_t *)&mock_binary, 4, rcv_data);
    }
    else if (message_expectation == MMI_MULTI)
    {
        length = robusto_make_multi_message(MSG_MESSAGE, 0, 0, (uint8_t *)&mock_strings, 8, (uint8_t *)&mock_binary, 4, rcv_data);
    }
    else if (message_expectation == MMI_BINARY_RESTRICTED)
    {
        // Fletch16, 2 zeroes, context and testdata
        char tmp[9] = "\x00\x00\x00\x00\x40" MOCK_BINARY;
        // Add prefix bytes
        length = ROBUSTO_PREFIX_BYTES + sizeof(tmp);
        *rcv_data = robusto_malloc(length);
        memcpy(*rcv_data + ROBUSTO_PREFIX_BYTES , tmp, sizeof(tmp));
        uint16_t fl16 = 0;
        robusto_checksum16(*rcv_data + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, ROBUSTO_CONTEXT_BYTE_LEN + 4, &fl16);
        memcpy(*rcv_data + ROBUSTO_PREFIX_BYTES, &fl16, 2);

    }
    else if (message_expectation == MMI_HI)
    {
        // Create a peer just to have a recipient
        uint8_t fake_mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        robusto_add_init_new_peer("TEST_EXTERNAL_PEER", &fake_mac, robusto_mt_mock);
        *peer = robusto_peers_find_peer_by_name("TEST_MOCK");
        (*peer)->base_mac_address[0] = 0xFF;
        (*peer)->mock_info.last_receive = r_millis();    
       
        length = robusto_make_presentation(*peer, rcv_data, false, presentation_add);
        ROB_LOGI("MOCK", "Generated presentation");
        rob_log_bit_mesh(ROB_LOG_INFO, "MOCK", *rcv_data, length);
    }
    else if (message_expectation == MMI_HEARTBEAT)
    {
        
        ROB_LOGI("MOCK", "mock_read_data: Mocking that heartbeat");

        *peer = robusto_peers_find_peer_by_name("TEST_MOCK");
        if ((*peer) != NULL) {
            (*peer)->mock_info.last_receive = r_millis();
            (*peer)->mock_info.last_peer_receive = r_millis()-40;
        } else {
            ROB_LOGE("MOCK", "TEST_MOCK not found!");
        }
        
        length = 0;
    }

    else
    {
        ROB_LOGI(mock_messaging_log_prefix, "Invalid message expectation on mock message generation");
        set_message_expectation(MMI_NONE);
        return ROB_FAIL;
    }
    // This has to be set at all times (especially when called async)
    set_message_expectation(MMI_NONE);

    
    return length;
}

rob_ret_val_t mock_send_receipt(robusto_peer_t *peer, bool receipt)
{

    int ret = ROB_OK;
    return ret;
}

void mock_do_on_work_cb(media_queue_item_t *queue_item)
{
    ROB_LOGD(mock_messaging_log_prefix, ">> In mock work callback.");
    send_work_item(queue_item, &(queue_item->peer->mock_info), robusto_mt_mock, &mock_send_message, &mock_do_on_poll_cb, mock_get_queue_context());
    ROB_LOGI(mock_messaging_log_prefix, ">> After calling send_work_item.");
}

void mock_do_on_poll_cb(queue_context_t *q_context)
{
    if ((get_message_expectation() == MMI_STRINGS_ASYNC) ||
        (get_message_expectation() == MMI_HI) ||
        (get_message_expectation() == MMI_SERVICE) ||
        (get_message_expectation() == MMI_HEARTBEAT))
    {
        uint8_t *data;
        uint8_t offset = ROBUSTO_PREFIX_BYTES;
        robusto_peer_t *peer = NULL;
        if (get_message_expectation() == MMI_HEARTBEAT)
        {
            // We reset the expectation inside mock_read_data, that is why the if has to be before..
            int data_length = mock_read_data(&data, &peer);
            return;
        }
        int data_length = mock_read_data(&data, &peer);
        
        if (data_length > 0)
        {
            //ROB_LOGI(mock_messaging_log_prefix, "Calling handle incoming");
            robusto_handle_incoming(data, data_length, peer, robusto_mt_mock, offset);
        }
        else
        {
            ROB_LOGE(mock_messaging_log_prefix, "Mock message returned an error: %i", data_length);
        }
    }
}

void mock_messaging_init(char *_log_prefix)
{
    mock_messaging_log_prefix = _log_prefix;
}

#endif