/**
 * @file canbus_control.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief CAN bus initialization and deinitialization
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
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS

#include "canbus_control.h"
#include "robusto_logging.h"

#include "canbus_queue.h"
#include "canbus_messaging.h"
#include "canbus_peer.h"
#include <robusto_media.h>
#define CAN bus_TIMEOUT_MS 80


#define TEST_DATA_LENGTH_KB 10
#define TEST_DATA_MULTIPLIER 100

#define CAN bus_TX_BUF_KB (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) /*!< CAN bus master doesn't need buffer */
#define CAN bus_RX_BUF_KB (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) /*!< CAN bus master doesn't need buffer */

#define TEST_DELAY_MS 100
#if (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) % 10 != 0
#error "TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER must be divideable by 10!"
#endif

#if CONFIG_CANBUS_ADDR == -1
#error "CANBUS - An CAN bus address must be set in menuconfig!"
#endif

const char * canbus_log_prefix;


void robusto_canbus_stop() {
    ROB_LOGI(canbus_log_prefix, "Shutting down canbus:");
    //TODO: Something needed here?
    ROB_LOGI(canbus_log_prefix, "CAN bus shut down.");
}


void robusto_canbus_start(char * _log_prefix) {
    canbus_compat_messaging_start();

    ROB_LOGI(canbus_log_prefix, "Starting CAN bus worker");
    if (canbus_init_worker(&canbus_do_on_work_cb, &canbus_do_on_poll_cb, canbus_log_prefix) != ROB_OK)
    {
       ROB_LOGE(canbus_log_prefix, "Failed initializing CAN bus"); 
       return;
    }
    canbus_set_queue_blocked(false);

    //canbus_receive();
    add_host_supported_media_type(robusto_mt_canbus);


    ROB_LOGI(canbus_log_prefix, "CANBUS initialized.");


}

/**
 * @brief Initialize canbus
 * 
 * @param _log_prefix 

 */
void robusto_canbus_init(char * _log_prefix) {
    canbus_log_prefix = _log_prefix;
    ROB_LOGI(canbus_log_prefix, "Initializing CAN bus");

    #if CONFIG_ROB_NETWORK_TEST_CANBUS_KILL_SWITCH > -1
        ROB_LOGE("----", "CANBUS KILL SWITCH ENABLED - GPIO %i", CONFIG_ROB_NETWORK_TEST_CANBUS_KILL_SWITCH);
        //robusto_gpio_set_direction(CONFIG_ROB_NETWORK_TEST_CANBUS_KILL_SWITCH, false);
        robusto_gpio_set_pullup(CONFIG_ROB_NETWORK_TEST_CANBUS_KILL_SWITCH, false);
        
        if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_CANBUS_KILL_SWITCH) == true)
        {
            ROB_LOGE("----", "CANBUS KILL SWITCH ON");
        } else {
            ROB_LOGE("----", "CANBUS KILL SWITCH OFF");
        }
    #endif

    canbus_peer_init(canbus_log_prefix);
    canbus_messaging_init(canbus_log_prefix);
    
}


#endif