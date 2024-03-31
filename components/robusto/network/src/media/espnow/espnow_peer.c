
/**
 * @file espnow_peer.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP-NOW peer functionality
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

#include <robconfig.h>
#if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) 

#include "espnow_peer.h"

#include <esp_now.h>

#include <robusto_logging.h>
#include <string.h>

#include <robusto_peer.h>
#include <robusto_time.h>
#include <robusto_qos.h>

#include <inttypes.h>
#include "espnow_messaging.h"

static char * espnow_peer_log_prefix;

uint32_t espnow_unknown_failures = 0;
uint32_t espnow_crc_failures = 0;


#if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) 
esp_now_peer_info_t* espnow_add_peer(uint8_t *mac_adress)
{
    esp_now_peer_info_t *esp_now_peer = malloc(sizeof(esp_now_peer_info_t));
    if (esp_now_get_peer(mac_adress, esp_now_peer) == ESP_OK) {
        ROB_LOGI(espnow_peer_log_prefix, "Tried to re-add an already existing ESP-NOW-peer. MAC address:");
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_peer_log_prefix, mac_adress, ROBUSTO_MAC_ADDR_LEN);
        return esp_now_peer;
    }
    /* Add broadcast peer information to peer list. */
    
    if (esp_now_peer == NULL)
    {
        ROB_LOGE(espnow_peer_log_prefix, "Malloc peer information fail");
        return ESP_FAIL;
    }
    
    memset(esp_now_peer, 0, sizeof(esp_now_peer_info_t));
    esp_now_peer->channel = CONFIG_ESPNOW_CHANNEL;
    esp_now_peer->ifidx = ESPNOW_WIFI_IF;
    esp_now_peer->encrypt = false;
    memcpy(esp_now_peer->peer_addr, mac_adress, ESP_NOW_ETH_ALEN);
    int rc = esp_now_add_peer(esp_now_peer);
    if (rc != 0) {
        if (rc == ESP_ERR_ESPNOW_NOT_INIT) {
            ROB_LOGE(espnow_peer_log_prefix,"Error adding ESP-NOW-peer: ESPNOW is not initialized");
        } else
        if (rc == ESP_ERR_ESPNOW_ARG) {
            ROB_LOGE(espnow_peer_log_prefix,"Error adding ESP-NOW-peer: Invalid argument (bad espnow_peer object?)");
        } else
        if (rc == ESP_ERR_ESPNOW_FULL) {
            ROB_LOGE(espnow_peer_log_prefix,"Error adding ESP-NOW-peer: The peer list is full");
        } else
        if (rc == ESP_ERR_ESPNOW_NO_MEM) {
            ROB_LOGE(espnow_peer_log_prefix,"Error adding ESP-NOW-peer: Out of memory");
        } else
        if (rc == ESP_ERR_ESPNOW_EXIST) {
            ROB_LOGE(espnow_peer_log_prefix,"Error adding ESP-NOW-peer: Peer has existed");
        } 

    }
    return esp_now_peer;
}
#endif


void espnow_stat_reset(void) {
    espnow_unknown_failures = 0;
    espnow_crc_failures = 0;
}

void espnow_peer_init_peer(robusto_peer_t *peer)
{
    memset(peer->espnow_info.failure_rate_history, 0, sizeof(float) * FAILURE_RATE_HISTORY_LENGTH);
    espnow_add_peer(&peer->base_mac_address);
    peer_stat_reset(&peer->espnow_info);
    set_state(peer, &peer->espnow_info, robusto_mt_espnow, media_state_initiating, media_problem_none);
}


void espnow_peer_init(char * _log_prefix) {
    espnow_peer_log_prefix = _log_prefix;
}

#endif