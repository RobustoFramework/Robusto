/**
 * @file mock_messaging.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief This is a mock implementation of a general messaging media
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
#pragma once
#include <robconfig.h>
#include <robusto_peer.h>
#include <robusto_retval.h>
#include <robusto_message.h>

#include "mock_queue.h"


//TODO: Is this a separate thing?
int mock_heartbeat(robusto_peer_t *peer);


/**
 * @brief Moc mode setting initialization
 *
 * @param is_master To set to master mode, set this to true. False for slave mode.
 * @param dont_delete Don't delete the driver (likely because it is the first call)
 * @return esp_err_t 
 */

rob_ret_val_t mock_before_comms(bool first_param, bool second_param);
rob_ret_val_t mock_after_comms(bool first_param, bool second_param);

rob_ret_val_t mock_send_message(robusto_peer_t *peer,uint8_t *data, uint32_t data_length, bool receipt);
rob_ret_val_t mock_read_receipt(robusto_peer_t * peer, uint8_t **dest_data);
int mock_read_data (uint8_t **rcv_data, robusto_peer_t **peer);
rob_ret_val_t mock_send_receipt(robusto_peer_t *peer, bool receipt);

void mock_do_on_work_cb(media_queue_item_t *queue_item);
void mock_do_on_poll_cb(queue_context_t *q_context);

void mock_messaging_init(char * _log_prefix);


