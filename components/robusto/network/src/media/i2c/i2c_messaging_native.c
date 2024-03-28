/**
 * @file i2c_messaging_arduino.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Native I2C implementation (this is a dummy implementation)
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
#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF))

#include <robusto_peer.h>
#include <robusto_logging.h>
#include "i2c_queue.h"

static char *i2c_native_messaging_log_prefix;

rob_ret_val_t i2c_before_comms(bool is_sending, bool first_call)
{
    // If sending set master

    return ROB_OK;
}
rob_ret_val_t i2c_after_comms(bool is_sending, bool first_call)
{

    // if sending set slave
    return ROB_OK;
}
rob_ret_val_t i2c_read_receipt(robusto_peer_t * peer) {
    return ROB_FAIL;
}
int i2c_read_data (uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes) {
    return ROB_FAIL;
}
rob_ret_val_t i2c_send_receipt(robusto_peer_t *peer, bool success) {
    return ROB_FAIL;
}

int i2c_heartbeat(robusto_peer_t *peer) {
    ROB_LOGE(i2c_native_messaging_log_prefix, "Called the *NATIVE* I2c implementation of i2c hearbeat, which isn't a thing.");
    return ROB_FAIL;
}
rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, int data_length, e_media_queue_item_type queue_item_type, bool receipt) {
    ROB_LOGE(i2c_native_messaging_log_prefix, "Called the *NATIVE* I2c implementation of send message, which isn't a thing.");
    return ROB_FAIL;
}
#if 0
void i2c_do_on_work_cb(media_queue_item_t *work_item) {}
void i2c_do_on_poll_cb(queue_context_t *q_context) {}
#endif
void i2c_messaging_init(char * _log_prefix)
{
    i2c_native_messaging_log_prefix = _log_prefix;
}

#endif