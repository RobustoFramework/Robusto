/**
 * @file lora_recover.c
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

#include "lora_recover.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <robusto_peer.h>
#include <robusto_logging.h>
#include <robusto_concurrency.h>
#include <robusto_message.h>
#include <robusto_qos.h>

static char *lora_recovery_log_prefix;

void lora_recover(recover_params_t * params)
{
    params->info->postpone_qos = true;
    ROB_LOGW(lora_recovery_log_prefix, "LoRa recovery: Recovering %s.", params->peer->name);
    robusto_peer_t * peer = params->peer;
    e_media_type media_type = robusto_mt_lora;
    set_state(peer, params->info, media_type, media_state_recovering, media_problem_none);
    rob_ret_val_t rec_retval = ROB_FAIL;
    // We are not presenting, the peer is not unknown, all media types have problems for this peer, we might either be out of range, or assume that we have been forgotten
    if ((peer->state > PEER_PRESENTING) && (peer->supported_media_types == peer->problematic_media_types))
    {
        params->info->postpone_qos = true;
        ROB_LOGW(lora_recovery_log_prefix, "All medias have problems for peer %s, we may be forgotten, trigger presentation using %s.", peer->name, media_type_to_str(media_type));
        if (robusto_send_presentation(peer, media_type, false, presentation_recover) == ROB_OK) {
            set_state(peer, params->info, media_type, media_state_working, media_problem_none);
            rec_retval = ROB_OK;
        }
    }
    if (rec_retval != ROB_OK) {
        // TODO: Go down to a lower LoRa speed setting. Can we have unique settings for a peer in a network? Does it take very little time to reconfigure?
        ROB_LOGW(lora_recovery_log_prefix, "Cannot really do much for LoRa here currently, reverts it to \"problem\" so it can return through heart beats");
        set_state(peer, params->info, media_type, media_state_problem, media_problem_unknown);
        params->info->postpone_qos = false;
    }

    robusto_delete_current_task();

}

void init_lora_recover(char *_log_prefix)
{
    lora_recovery_log_prefix = _log_prefix;
}
#endif