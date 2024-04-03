/**
 * @file canbus_messaging.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief CAN bus messaging implementation
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

#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS


#include <robusto_peer.h>
#include <robusto_retval.h>
#include <robusto_message.h>
#include "canbus_queue.h"


#ifdef CONFIG_ROBUSTO_SIM
#include "canbus_simulate.h"
#endif


#ifdef __cplusplus
extern "C"
{
#endif


#if CONFIG_CANBUS_ADDR == -1
#error "CAN bus - A CAN bus address must be set in menuconfig!"
#endif

#define CANBUS_MESSAGE_OFFSET (ROBUSTO_CRC_LENGTH + ROBUSTO_PREFIX_BYTES)
#define CANBUS_TIMEOUT_MS 200
#define CANBUS_MAX_PACKETS 2048 // Makes the maximum message length 16 384 bytes

/**
 * @brief CAN bus mode setting initialization
 *
 * @param is_master To set to master mode, set this to true. False for slave mode.
 * @param dont_delete Don't delete the driver (likely because it is the first call)
 * @return esp_err_t 
 */

rob_ret_val_t canbus_before_comms(bool first_param, bool second_param);
rob_ret_val_t canbus_after_comms(bool first_param, bool second_param);

void canbus_handle_incoming(uint8_t * data, uint32_t data_length);


void canbus_do_on_work_cb(media_queue_item_t *queue_item);
void canbus_do_on_poll_cb(queue_context_t *q_context);
void canbus_messaging_init(char * _log_prefix);

/* Implemented in compatibility layers */

rob_ret_val_t canbus_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);
int canbus_read_data (uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes);

rob_ret_val_t canbus_read_receipt(robusto_peer_t * peer);
rob_ret_val_t canbus_send_receipt(robusto_peer_t *peer, bool success, bool unknown);

int canbus_heartbeat(robusto_peer_t *peer);

void canbus_compat_messaging_start(void);
void canbus_compat_messaging_init(char * _log_prefix);



#ifdef __cplusplus
} /* extern "C" */
#endif
#endif