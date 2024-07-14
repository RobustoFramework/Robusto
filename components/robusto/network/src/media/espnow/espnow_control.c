/**
 * @file espnow_control.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The ESP-NOW media implements the proprietary ESP-NOW protocol by Espressif
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

#include "robconfig.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include "espnow_control.h"

// #include "../secret/local_settings.h"

#include <robusto_logging.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "robusto_peer.h"

#include "espnow_messaging.h"
#include "espnow_peer.h"
#include <robusto_media.h>
#include <esp_adc/adc_oneshot.h>

/* The log prefix for all logging */
static char *espnow_log_prefix;

void init_wifi()
{
    ROB_LOGI(espnow_log_prefix, "Creating default event loop.");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ROB_LOGI(espnow_log_prefix, "Initializing wifi (for ESP-NOW)");
    ESP_ERROR_CHECK(esp_netif_init());
    ROB_LOGI(espnow_log_prefix, "esp_netif_init done.");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ROB_LOGI(espnow_log_prefix, "esp_wifi_init done.");
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ROB_LOGI(espnow_log_prefix, "esp_wifi_set_storage done.");
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ROB_LOGI(espnow_log_prefix, "esp_wifi_set_mode done.");
    ESP_ERROR_CHECK(esp_wifi_start());
    ROB_LOGI(espnow_log_prefix, "esp_wifi_start done.");
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ROB_LOGI(espnow_log_prefix, "esp_wifi_set_channel done.");

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
#endif

#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    ROB_LOGE("----", "ESP-NOW KILL SWITCH ENABLED - GPIO %i", CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH);
    //robusto_gpio_set_direction(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH, false);
    robusto_gpio_set_pullup(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH, false);
    
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("----", "ESP-NOW KILL SWITCH ON");
    } else {
        ROB_LOGE("----", "ESP-NOW KILL SWITCH OFF");
    }
#endif
}

void robusto_espnow_stop()
{
    ROB_LOGI(espnow_log_prefix, "Shutting down ESP-NOW:");
    ROB_LOGI(espnow_log_prefix, " - wifi stop");
    esp_wifi_stop();
    ROB_LOGI(espnow_log_prefix, " - wifi disconnect");
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_NULL);
    ROB_LOGI(espnow_log_prefix, " - wifi deinit");
    esp_wifi_deinit();
    ROB_LOGI(espnow_log_prefix, "ESP-NOW shut down.");
}


void robusto_espnow_start() {
   ROB_LOGI(espnow_log_prefix, "Starting ESP-NOW.");
    init_wifi();

    uint8_t wifi_mac_addr[ROBUSTO_MAC_ADDR_LEN];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, wifi_mac_addr);
    ROB_LOGW(espnow_log_prefix, "robusto_espnow_start - WIFI STA MAC address:");
    rob_log_bit_mesh(ROB_LOG_WARN, espnow_log_prefix, wifi_mac_addr, ROBUSTO_MAC_ADDR_LEN);
        
    espnow_messaging_init(espnow_log_prefix);
    espnow_peer_init(espnow_log_prefix);

    if (espnow_init_worker(espnow_do_on_work_cb, NULL, espnow_log_prefix) != ROB_OK)
    {
        ROB_LOGE(espnow_log_prefix, "Failed initializing ESP-NOW worker");
        return;
    }
    espnow_set_queue_blocked(false);

    add_host_supported_media_type(robusto_mt_espnow);
    ROB_LOGI(espnow_log_prefix, "ESP-NOW started.");
}

/**
 * @brief Initialize ESP-NOW
 *
 * @param _log_prefix
 */
void robusto_espnow_init(char *_log_prefix)
{
    espnow_log_prefix = _log_prefix;
}

#endif