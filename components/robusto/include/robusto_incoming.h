
/**
 * @file robusto_handler.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Handling of incoming data
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

#include <stdint.h>
#include <robusto_retval.h>
#include <robusto_queue.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#ifndef USE_ARDUINO
#include <robusto_sys_queue.h>
#endif


#ifdef __cplusplus
extern "C"
{
#endif




typedef struct incoming_queue_item
{
    /* Message*/
    robusto_message_t *message;
    /* The service frees the message itself */
    bool recipient_frees_message;
    /* Queue reference */
    STAILQ_ENTRY(incoming_queue_item)
    items;
} incoming_queue_item_t;



/**
 * These callbacks are implemented to handle the different
 * work types.
 */
/* Callbacks that handles incoming work items */
typedef void (incoming_callback_cb)(incoming_queue_item_t *incoming_item);

rob_ret_val_t robusto_register_handler(incoming_callback_cb *_incoming_callback);

rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t data_length, robusto_peer_t *peer, e_media_type media_type, int offset);
rob_ret_val_t robusto_handle_network(incoming_queue_item_t * queue_item);
rob_ret_val_t robusto_handle_service(incoming_queue_item_t *queue_item);

rob_ret_val_t incoming_safe_add_work_queue(incoming_queue_item_t * queue_item);

rob_ret_val_t incoming_init_worker(incoming_callback_cb work_cb, char *_log_prefix);
rob_ret_val_t robusto_incoming_network_init(char * _log_prefix);

void incoming_set_queue_blocked(bool blocked);

void incoming_shutdown_worker();

queue_context_t *incoming_get_queue_context();

#ifdef __cplusplus
} /* extern "C" */
#endif