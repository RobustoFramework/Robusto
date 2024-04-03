/**
 * @file robusto_qos.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto Quality-of-service functionality
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

#include <robusto_media_def.h>
#include <robusto_peer_def.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The message context for heart beats is always 0x43 (MSG_HEARTBEAT + binary data)
#define HEARTBEAT_CONTEXT 0x03 + 0x40 



typedef struct recover_params {
    robusto_peer_t *peer;
    robusto_media_t *info;
    TaskFunction_t custom_recovery;
    e_media_type media_type;
    uint64_t last_heartbeat_time;
} recover_params_t;

typedef void(on_state_change_t)(robusto_peer_t * peer, robusto_media_t *info, e_media_type media_type, e_media_state media_state, e_media_problem problem);
/**
 * @brief Register a callback to react if the state of a peer changes.
 * 
 * @param _cb_on_state_change Callback, should not take long to not interfere with monitoring
 */
void robusto_qos_register_on_state_change(on_state_change_t *_cb_on_state_change);

float add_to_failure_rate_history(robusto_media_t *stats, float rate);
void add_to_history(robusto_media_t * stats, bool sending, rob_ret_val_t result);
uint32_t robusto_calc_suitability(e_media_type media_type, uint32_t payloadSize);
void set_state(robusto_peer_t * peer, robusto_media_t *info, e_media_type media_type, e_media_state media_state, e_media_problem problem);
void check_media(robusto_peer_t * peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void send_heartbeat_message(robusto_peer_t *peer, e_media_type media_type);
uint64_t parse_heartbeat(uint8_t * data, uint8_t preamble_len);


#ifdef __cplusplus
} /* extern "C" */
#endif