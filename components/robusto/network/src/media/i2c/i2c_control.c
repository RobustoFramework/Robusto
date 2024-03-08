/**
 * @file i2c_control.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief I2C initialization and deinitialization
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
#if CONFIG_ROBUSTO_SUPPORTS_I2C

#include "i2c_control.h"
#include "robusto_logging.h"

#include "i2c_queue.h"
#include "i2c_messaging.h"
#include "i2c_peer.h"
#include <robusto_media.h>
#define I2C_TIMEOUT_MS 80


#define TEST_DATA_LENGTH_KB 10
#define TEST_DATA_MULTIPLIER 100

#define I2C_TX_BUF_KB (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) /*!< I2C master doesn't need buffer */
#define I2C_RX_BUF_KB (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) /*!< I2C master doesn't need buffer */

#define TEST_DELAY_MS 100
#if (TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER) % 10 != 0
#error "TEST_DATA_LENGTH_KB * TEST_DATA_MULTIPLIER must be divideable by 10!"
#endif

#if CONFIG_I2C_ADDR == -1
#error "I2C - An I2C address must be set in menuconfig!"
#endif

const char * i2c_log_prefix;


void robusto_i2c_stop() {
    ROB_LOGI(i2c_log_prefix, "Shutting down i2c:");
    //TODO: Something needed here?
    ROB_LOGI(i2c_log_prefix, "i2c shut down.");
}


void robusto_i2c_start(char * _log_prefix) {
    i2c_compat_messaging_start();

    ROB_LOGI(i2c_log_prefix, "Starting I2C worker");
    if (i2c_init_worker(&i2c_do_on_work_cb, &i2c_do_on_poll_cb, i2c_log_prefix) != ROB_OK)
    {
       ROB_LOGE(i2c_log_prefix, "Failed initializing I2C"); 
       return;
    }
    i2c_set_queue_blocked(false);

    //i2c_receive();
    add_host_supported_media_type(robusto_mt_i2c);


    ROB_LOGI(i2c_log_prefix, "I2C initialized.");


}

/**
 * @brief Initialize i2c
 * 
 * @param _log_prefix 

 */
void robusto_i2c_init(char * _log_prefix) {
    i2c_log_prefix = _log_prefix;
    ROB_LOGI(i2c_log_prefix, "Initializing I2C");

    #if CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH > -1
        ROB_LOGE("----", "I2C KILL SWITCH ENABLED - GPIO %i", CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH);
        //robusto_gpio_set_direction(CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH, false);
        robusto_gpio_set_pullup(CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH, false);
        
        if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH) == true)
        {
            ROB_LOGE("----", "I2C KILL SWITCH ON");
        } else {
            ROB_LOGE("----", "I2C KILL SWITCH OFF");
        }
    #endif

    i2c_peer_init(i2c_log_prefix);
    i2c_messaging_init(i2c_log_prefix);
    
}


#endif