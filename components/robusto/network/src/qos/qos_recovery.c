
/**
 * @file qos_recovery.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief QoS failure recovery
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

#include <robusto_media.h>
#include <robusto_peer.h>
#include <robusto_time.h>
#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_qos.h>
#include <robusto_concurrency.h>
#include "qos_recovery.h"
#include "robusto_qos_init.h"

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "../media/mock/mock_recover.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "../media/lora/lora_recover.h"
#endif
static char *recovery_log_prefix;

void task_recover(recover_params_t *params)
{

    params->info->postpone_qos = true;
    robusto_peer_t *peer = params->peer;
    e_media_type media_type = params->media_type;
    char *str_media_type =  media_type_to_str(media_type);
    ROB_LOGW(recovery_log_prefix, "%s recovery: Recovering %s.%hu, %hu, %hu, %hu, %hu ",
             str_media_type, peer->name, get_host_peer()->supported_media_types,
             (peer->supported_media_types & get_host_peer()->supported_media_types),
             peer->state, peer->supported_media_types, peer->problematic_media_types);
    // TODO: We have a custom recovery function being passed, how and when is that going to be called? And what parameters should it take?
    set_state(peer, params->info, media_type, media_state_recovering, media_problem_unknown);
    rob_ret_val_t rec_retval = ROB_FAIL;
    // We are not presenting, the peer is not unknown
    if (peer->state > PEER_PRESENTING)
    {
        // Postpone other QoS actions, like heart beats.
        params->info->postpone_qos = true;
        if ((peer->supported_media_types & get_host_peer()->supported_media_types) == peer->problematic_media_types)
        {
            // all media types have problems for this peer, we might either be out of range, or assume that we have been forgotten
            ROB_LOGW(recovery_log_prefix, "All medias have problems for peer %s, we may be forgotten, trigger presentation using %s.", 
                peer->name, str_media_type);
            if (robusto_send_presentation(peer, media_type, false, presentation_recover) == ROB_OK)
            {
                set_state(peer, params->info, media_type, media_state_working, media_problem_none);
                rec_retval = ROB_OK;
            } else {
                set_state(peer, params->info, media_type, media_state_problem, media_problem_unknown);
                ROB_LOGW(recovery_log_prefix, ">> Sending presentation failed");
                rec_retval = ROB_FAIL;
            }
        }
        params->info->postpone_qos = false;
        if (rec_retval != ROB_OK)
        {
            ROB_LOGW(recovery_log_prefix, ">> Recovering peer %s, %s. Sending heartbeat.", peer->name, str_media_type);
            send_heartbeat_message(peer, media_type);
        }
    }
    
    robusto_delete_current_task();
}

void create_recovery_task(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type, TaskFunction_t custom_recovery)
{
    ROB_LOGI(recovery_log_prefix, "Creating a recovery task for peer %s, media %s", peer->name, media_type_to_str(media_type));
    recover_params_t *params = robusto_malloc(sizeof(recover_params_t));
    params->peer = peer;
    params->info = info;
    params->last_heartbeat_time = last_heartbeat_time;
    params->custom_recovery = custom_recovery;
    params->media_type = media_type;
    char *task_name;
    robusto_asprintf(&task_name, "Recovery task for peer %s, media %s", peer->name, media_type_to_str(media_type));
    if (robusto_create_task(&task_recover, params, task_name, NULL, 0) != ROB_OK)
    {
        ROB_LOGE(recovery_log_prefix, "Failed creating a recovery task for the peer %s, media %s ", peer->name, media_type_to_str(media_type));
    }
    robusto_free(task_name);
    // Always wait a while before returning to avoid other tasks being missing the PEER_PRESENTING state.
    r_delay(300);
}

void recover_media(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat, e_media_type media_type)
{

    // At some point we need to try and actively recover a media 

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    if (media_type == robusto_mt_lora)
    {
        create_recovery_task(peer, info, last_heartbeat, media_type, &lora_recover);
        return;
    } 
#endif

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
    if (media_type == robusto_mt_mock)
    {
        // We won't actually do any recovering.
        // Note that mock_recover.c can be used here.
        return;
    } 
#endif
    // Normal recovery without any custom stuff
    create_recovery_task(peer, info, last_heartbeat, media_type, NULL);
    

}

void start_qos_recovery()
{
    // We should not have any relations now if the boot was intentional
    if (get_relation_count() > 0)
    {
#if defined(CONFIG_ROBUSTO_CONDUCTOR_SERVER) || defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT)
        ROB_LOGI(recovery_log_prefix, "Restarting a conductor server/client, assuming from deep sleep, re-adding relation as peers.");
#else
        ROB_LOGW(recovery_log_prefix, "Obviously, we are recovering from some situation, re-adding relations as peers.");
#endif
        recover_relations();
    }
    else
    {
        ROB_LOGI(recovery_log_prefix, "Nothing to recover");
    }
}

void init_qos_recovery(char *_log_prefix)
{
    recovery_log_prefix = _log_prefix;
}