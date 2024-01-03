/**
 * @file espnow_recover.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP-NOW connection recovery implementation. 
 * @todo: Implement this for real where applicable
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

#include "espnow_recover.h"

#include <robusto_peer.h>
#include <robusto_logging.h>
#include <robusto_concurrency.h>
#include <robusto_message.h>
#include <robusto_qos.h>

static char *espnow_recovery_log_prefix;

void espnow_recover(recover_params_t * params)
{
    params->info->postpone_qos = true;
    ROB_LOGW(espnow_recovery_log_prefix, "ESP-NOW recovery: Recovering %s.", params->peer->name);
    
    params->info->state = media_state_recovering;
    params->peer->state = PEER_UNKNOWN;
    ROB_LOGI(espnow_recovery_log_prefix, "ESP-NOW recovery: Will try sending a presentation.");
   
    robusto_send_presentation(params->peer, robusto_mt_espnow, false);
    params->info->postpone_qos = true;
    if (peer_waitfor_at_least_state(params->peer, PEER_KNOWN_INSECURE, 6000)) {
        params->info->state = media_state_working;
    } else {
        params->info->state = media_state_recovering;
    }
    robusto_delete_current_task();

}

void init_espnow_recover(char *_log_prefix)
{
    espnow_recovery_log_prefix = _log_prefix;
}