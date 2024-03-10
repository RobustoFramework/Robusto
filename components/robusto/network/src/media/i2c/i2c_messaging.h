/**
 * @file i2c_messaging.h
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

#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C


#include <robusto_peer.h>
#include <robusto_retval.h>
#include <robusto_message.h>
#include "i2c_queue.h"


#ifdef CONFIG_ROBUSTO_SIM
#include "i2c_simulate.h"
#endif


#ifdef __cplusplus
extern "C"
{
#endif


#if CONFIG_I2C_ADDR == -1
#error "I2C - An I2C address must be set in menuconfig!"
#endif

#define ACK_CHECK_EN 0x1  /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0 /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0       /*!< I2C ack value */
#define NACK_VAL 0x1      /*!< I2C nack value */

#define I2C_TIMEOUT_MS 1000


#define I2C_TX_BUF 1000 /*!< I2C master doesn't need buffer */
#define I2C_RX_BUF 1000 /*!< I2C master doesn't need buffer */

#define I2C_FRAGMENT_SIZE (I2C_TX_BUF - 10)

/**
 * @brief i2c mode setting initialization
 *
 * @param is_master To set to master mode, set this to true. False for slave mode.
 * @param dont_delete Don't delete the driver (likely because it is the first call)
 * @return esp_err_t 
 */

rob_ret_val_t i2c_before_comms(bool first_param, bool second_param);
rob_ret_val_t i2c_after_comms(bool first_param, bool second_param);

void i2c_handle_incoming(uint8_t * data, uint32_t data_length);

void i2c_do_on_work_cb(media_queue_item_t *queue_item);
void i2c_do_on_poll_cb(queue_context_t *q_context);
void i2c_messaging_init(char * _log_prefix);

/* Implemented in compatibility layers */

rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);
int i2c_read_data (uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes);

int i2c_heartbeat(robusto_peer_t *peer);

rob_ret_val_t i2c_read_receipt(robusto_peer_t * peer);
rob_ret_val_t i2c_send_receipt(robusto_peer_t *peer, bool success, bool unknown);

void i2c_compat_messaging_start(void);
void i2c_compat_messaging_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif