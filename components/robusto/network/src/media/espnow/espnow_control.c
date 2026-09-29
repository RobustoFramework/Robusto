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
static bool espnow_signal_status_valid;
static int8_t espnow_tx_power_qdbm;
static uint8_t espnow_primary_channel;
static uint8_t espnow_protocol_bitmap;

#ifdef CONFIG_ESP_PHY_MAX_WIFI_TX_POWER
#define ROBUSTO_ESPNOW_CONFIG_TX_POWER_DBM ((int8_t)CONFIG_ESP_PHY_MAX_WIFI_TX_POWER)
#define ROBUSTO_ESPNOW_REQUESTED_TX_POWER_QDBM ((int8_t)(ROBUSTO_ESPNOW_CONFIG_TX_POWER_DBM * 4))
#else
#error "CONFIG_ESP_PHY_MAX_WIFI_TX_POWER must be defined when ESP-NOW support is enabled"
#endif

static int8_t espnow_expected_tx_power_readback(int8_t requested_tx_power_qdbm)
{
    if (requested_tx_power_qdbm <= 19) { return 8; }
    if (requested_tx_power_qdbm <= 27) { return 20; }
    if (requested_tx_power_qdbm <= 33) { return 28; }
    if (requested_tx_power_qdbm <= 43) { return 34; }
    if (requested_tx_power_qdbm <= 51) { return 44; }
    if (requested_tx_power_qdbm <= 55) { return 52; }
    if (requested_tx_power_qdbm <= 59) { return 56; }
    if (requested_tx_power_qdbm <= 65) { return 60; }
    if (requested_tx_power_qdbm <= 71) { return 66; }
    if (requested_tx_power_qdbm <= 79) { return 72; }
    return 80;
}

static void log_wifi_signal_config(int8_t requested_tx_power_qdbm, int8_t expected_tx_power_qdbm)
{
    int8_t max_tx_power = 0;
    esp_err_t tx_power_rc = esp_wifi_get_max_tx_power(&max_tx_power);
    bool tx_power_valid = false;
    bool channel_valid = false;
    bool protocol_valid = false;
    uint8_t primary_channel = 0;
    uint8_t protocol_bitmap = 0;
    if (tx_power_rc == ESP_OK)
    {
        tx_power_valid = true;
        espnow_tx_power_qdbm = max_tx_power;
        ROB_LOGW(espnow_log_prefix,
                 "Wi-Fi max TX power readback=%d qdbm, requested=%d qdbm, expected_readback=%d qdbm, sdkconfig=%d dBm",
                 (int)max_tx_power,
                 (int)requested_tx_power_qdbm,
                 (int)expected_tx_power_qdbm,
                 (int)ROBUSTO_ESPNOW_CONFIG_TX_POWER_DBM);
        if (max_tx_power != expected_tx_power_qdbm)
        {
            ROB_LOGE(espnow_log_prefix,
                     "Wi-Fi TX power readback mismatch: requested=%d expected_readback=%d actual=%d (0.25 dBm units)",
                     (int)requested_tx_power_qdbm,
                     (int)expected_tx_power_qdbm,
                     (int)max_tx_power);
        }
    }
    else
    {
        ROB_LOGW(espnow_log_prefix, "esp_wifi_get_max_tx_power failed rc=%d", (int)tx_power_rc);
    }

    wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
    esp_err_t channel_rc = esp_wifi_get_channel(&primary_channel, &second_channel);
    if (channel_rc == ESP_OK)
    {
        channel_valid = true;
        espnow_primary_channel = primary_channel;
        ROB_LOGW(espnow_log_prefix,
                 "Wi-Fi channel readback primary=%u second=%u expected=%u",
                 (unsigned)primary_channel,
                 (unsigned)second_channel,
                 (unsigned)CONFIG_ESPNOW_CHANNEL);
        if (primary_channel != CONFIG_ESPNOW_CHANNEL || second_channel != WIFI_SECOND_CHAN_NONE)
        {
            ROB_LOGE(espnow_log_prefix,
                     "Wi-Fi channel readback mismatch: expected primary=%u second=%u actual primary=%u second=%u",
                     (unsigned)CONFIG_ESPNOW_CHANNEL,
                     (unsigned)WIFI_SECOND_CHAN_NONE,
                     (unsigned)primary_channel,
                     (unsigned)second_channel);
        }
    }
    else
    {
        ROB_LOGW(espnow_log_prefix, "esp_wifi_get_channel failed rc=%d", (int)channel_rc);
    }

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    const unsigned expected_lr = 1U;
#else
    const unsigned expected_lr = 0U;
#endif
    esp_err_t protocol_rc = esp_wifi_get_protocol(ESPNOW_WIFI_IF, &protocol_bitmap);
    if (protocol_rc == ESP_OK)
    {
        protocol_valid = true;
        espnow_protocol_bitmap = protocol_bitmap;
        ROB_LOGW(espnow_log_prefix,
                 "Wi-Fi protocol readback bitmap=0x%02x expected_lr=%u",
                 (unsigned)protocol_bitmap,
                 expected_lr);
#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
        if ((protocol_bitmap & WIFI_PROTOCOL_LR) == 0U)
        {
            ROB_LOGE(espnow_log_prefix,
                     "Wi-Fi protocol readback missing WIFI_PROTOCOL_LR: bitmap=0x%02x",
                     (unsigned)protocol_bitmap);
        }
#endif
    }
    else
    {
        ROB_LOGW(espnow_log_prefix, "esp_wifi_get_protocol failed rc=%d", (int)protocol_rc);
    }

    espnow_signal_status_valid = tx_power_valid && channel_valid && protocol_valid;

    wifi_country_t country = {0};
    esp_err_t country_rc = esp_wifi_get_country(&country);
    if (country_rc == ESP_OK)
    {
        ROB_LOGW(espnow_log_prefix,
                 "Wi-Fi country %c%c policy=%d schan=%u nchan=%u country_max_tx_power=%d",
                 country.cc[0],
                 country.cc[1],
                 (int)country.policy,
                 (unsigned)country.schan,
                 (unsigned)country.nchan,
                 (int)country.max_tx_power);
    }
    else
    {
        ROB_LOGW(espnow_log_prefix, "esp_wifi_get_country failed rc=%d", (int)country_rc);
    }
}

bool robusto_espnow_get_signal_status(int8_t *tx_power_qdbm,
                                      uint8_t *primary_channel,
                                      uint8_t *protocol_bitmap)
{
    if (!espnow_signal_status_valid || tx_power_qdbm == NULL || primary_channel == NULL || protocol_bitmap == NULL)
    {
        return false;
    }
    *tx_power_qdbm = espnow_tx_power_qdbm;
    *primary_channel = espnow_primary_channel;
    *protocol_bitmap = espnow_protocol_bitmap;
    return true;
}

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
    const int8_t expected_tx_power_qdbm = espnow_expected_tx_power_readback(ROBUSTO_ESPNOW_REQUESTED_TX_POWER_QDBM);
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(ROBUSTO_ESPNOW_REQUESTED_TX_POWER_QDBM));
    ROB_LOGI(espnow_log_prefix,
             "esp_wifi_set_max_tx_power requested=%d qdbm expected_readback=%d qdbm sdkconfig=%d dBm",
             (int)ROBUSTO_ESPNOW_REQUESTED_TX_POWER_QDBM,
             (int)expected_tx_power_qdbm,
             (int)ROBUSTO_ESPNOW_CONFIG_TX_POWER_DBM);

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ROB_LOGI(espnow_log_prefix, "ESP-NOW long-range protocol enabled");
#endif

    log_wifi_signal_config(ROBUSTO_ESPNOW_REQUESTED_TX_POWER_QDBM, expected_tx_power_qdbm);

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
    esp_wifi_get_mac(ESPNOW_WIFI_IF, wifi_mac_addr);
    ROB_LOGW(espnow_log_prefix, "robusto_espnow_start - WIFI STA MAC address:");
    rob_log_bit_mesh(ROB_LOG_WARN, espnow_log_prefix, wifi_mac_addr, ROBUSTO_MAC_ADDR_LEN);
        
    espnow_messaging_init(espnow_log_prefix);
    espnow_peer_init(espnow_log_prefix);

    if (espnow_init_worker((work_callback *)espnow_do_on_work_cb, NULL, espnow_log_prefix) != ROB_OK)
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