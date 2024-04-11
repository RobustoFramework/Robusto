
/**
 * @file robusto_media.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto Media initialization
 * @todo Rename to robusto_media_init.c?
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

#include "robusto_media.h"
#include "robusto_system.h"
#include "robusto_logging.h"
#include <string.h>

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "i2c/i2c_control.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#include "canbus/canbus_control.h"
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include "ble/ble_control.h"
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include "espnow/espnow_control.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "lora/lora_control.h"
#endif
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "mock/mock_control.h"
#endif


robusto_media_types host_supported_media_types = 0;
static char * media_log_prefix;

void list_media_types(robusto_media_types media_types, char *log_str)
{
    // Assume log string is long enough
    /*
    if (media_types & robusto_mt_ttl)
    {
        strcat(log_str, " TTL,");
    }*/
    if (media_types & robusto_mt_ble)
    {
        strcat(log_str, " BLE,");
    }
    if (media_types & robusto_mt_espnow)
    {
        strcat(log_str, " ESP-NOW,");
    }
    if (media_types & robusto_mt_lora)
    {
        strcat(log_str, " LoRa,");
    }
    if (media_types & robusto_mt_i2c)
    {
        strcat(log_str, " I2C,");
    }
    
    if (media_types & robusto_mt_canbus)
    {
        strcat(log_str, "CAN bus,");
    }
    /*
    if (media_types & robusto_mt_umts)
    {
        strcat(log_str, " UMTS,");
    }*/
    if (media_types & robusto_mt_mock)
    {
        strcat(log_str, " MOCK,");
    }
    if (media_types > 0)
    {
        // Remove last ","
        log_str[strlen(log_str) - 1] = 0;
    }
}

char *media_type_to_str(robusto_media_types media_type)
{
    // Assume log string is long enough
    if (media_type == robusto_mt_ble)
    {
        return "BLE";
    }
    if (media_type == robusto_mt_espnow)
    {
        return "ESP-NOW";
    }
    if (media_type == robusto_mt_lora)
    {
        return "LoRa";
    }
    if (media_type == robusto_mt_i2c)
    {
        return "I2C";
    }
    
    if (media_type == robusto_mt_canbus)
    {
        return "CAN bus";
    }
    /*
    if (media_type == robusto_mt_umts)
    {
        return "UMTS";
    }
    */
    if (media_type == robusto_mt_mock)
    {
        return "MOCK";
    } else
    {
        return "Unknown/multiple";
    }
}




void robusto_media_stop() {

    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    robusto_i2c_stop();
    #endif
    
    #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    robusto_canbus_stop();
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    robusto_espnow_stop();
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    robusto_lora_stop();
    #endif

}

void robusto_media_start() {

    uint64_t mem_before;
    uint64_t time_before;

    #ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    ROB_LOGW("mem", "Starting robusto_lora_start");
    mem_before = get_free_mem();
    time_before = r_millis();
    robusto_lora_start();
    ROB_LOGW("mem", "Started, took robusto_lora_start %llu ms and %llu bytes.", r_millis() - time_before, mem_before - get_free_mem());

    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C


    ROB_LOGW("mem", "Starting robusto_i2c_start");

    mem_before = get_free_mem();
    time_before = r_millis();
    robusto_i2c_start();

    ROB_LOGW("mem", "Started, took robusto_i2c_start %llu ms and %llu bytes.", r_millis() - time_before, mem_before - get_free_mem());

    
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS


    ROB_LOGW("mem", "Starting robusto_canbus_start");

    mem_before = get_free_mem();
    time_before = r_millis();
    robusto_canbus_start();

    ROB_LOGW("mem", "Started, took robusto_canbus_start %llu ms and %llu bytes.", r_millis() - time_before, mem_before - get_free_mem());

    
    #endif


    #ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW

    ROB_LOGW("mem", "Starting robusto_espnow_start");

    mem_before = get_free_mem();
    time_before = r_millis();
    robusto_espnow_start();

    ROB_LOGW("mem", "Started, took robusto_espnow_start %llu ms and %llu bytes.", r_millis() - time_before, mem_before - get_free_mem());

    
    #endif


}


void robusto_media_init(char * _log_prefix) {
    media_log_prefix = _log_prefix;

    #ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    robusto_lora_init(media_log_prefix);
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
    robusto_ble_init(media_log_prefix);
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    robusto_canbus_init(media_log_prefix);
    #endif

    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    robusto_i2c_init(media_log_prefix);
    #endif
    #ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    robusto_espnow_init(media_log_prefix);
    #endif

    #ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
    ROBUSTO_NETWORK_MOCK_TESTING_init(media_log_prefix);
    #endif


}

