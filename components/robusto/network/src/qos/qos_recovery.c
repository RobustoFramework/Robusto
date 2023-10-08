
/**
 * @file qos_recovery.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
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
char *recovery_log_prefix;


void create_recovery_task (robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type, TaskFunction_t task_function) {
    ROB_LOGI(recovery_log_prefix, "Creating a recovery task for peer %s, media %s", peer->name, media_type_to_str(media_type));
    recover_params_t * params = robusto_malloc(sizeof(recover_params_t));
    params->peer = peer;
    params->info = info;
    params->last_heartbeat_time = last_heartbeat_time;
    char * task_name;
    robusto_asprintf(&task_name, "Recovery task for peer %s, media %s", peer->name, media_type_to_str(media_type));
    if (robusto_create_task(task_function, params, task_name, NULL, 0) != ROB_OK) {
        ROB_LOGE(recovery_log_prefix, "Failed creatin a recovery task for the peer %s, media %s ", peer->name, media_type_to_str(media_type));
    }
    robusto_free(task_name);
}

void recover_media(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat, e_media_type media_type)
{

    // If we have not received a response for a while.
    // Regardless of the cause, Robusto peers completely disregard any peers that they don't know about. 
    // Hence, it might be a good idea to just try a presentation. 

#ifdef CONFIG_ROBUSTO_SUPPORTS_TTL


    if ((media_type == robusto_mt_ttl) && (peer->ttl_info.last_send < last_heartbeat))
    {
        // Not Implemented
    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
    if ((media_type == robusto_mt_ble) && (peer->ble_info.last_send < last_heartbeat))
    {
        // Not Implemented
    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    if ((media_type == robusto_mt_espnow) && (peer->espnow_info.last_send < last_heartbeat))
    {

    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    if ((media_type == robusto_mt_lora) && (peer->lora_info.last_send < last_heartbeat))
    {

    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    if ((media_type == robusto_mt_i2c) && (peer->i2c_info.last_send < last_heartbeat))
    {

    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    if (media_type == robusto_mt_canbus)
    {
        // Not Implemented
    }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_UMTS
    if (media_type == robusto_mt_umts)
    {
        // Not Implemented
    }
#endif
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
    if (media_type == robusto_mt_mock) {
        create_recovery_task(peer, info, last_heartbeat, media_type, &mock_recover);
    }
#endif    
}


void start_qos_recovery() {
    // We should not have any relations now if the boot was intentional
    if (get_relation_count() > 0) {
        ROB_LOGW(recovery_log_prefix, "Obviously, we are recovering from some situation, re-adding relations as peers.");
        recover_relations();
    } else {
        ROB_LOGI(recovery_log_prefix, "Nothing to recover"); 
    }
}


void init_qos_recovery(char *_log_prefix)
{
    recovery_log_prefix = _log_prefix;


}