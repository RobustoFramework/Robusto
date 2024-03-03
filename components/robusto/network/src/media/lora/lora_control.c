/**
 * @file lora_control.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief LoRa (from "long range") is a physical proprietary radio communication technique.
 * See wikipedia: https://en.wikipedia.org/wiki/LoRa
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

#include "lora_control.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

#include "lora_messaging.hpp"

// #include "../secret/local_settings.h"

#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_message.h>
#include "lora_peer.h"
#include <robusto_media.h>
#include "lora_queue.h"
#include "lora_recover.h"


/* The log prefix for all logging */
static char *lora_log_prefix;


void robusto_lora_start()
{
    ROB_LOGI(lora_log_prefix, "Starting LoRa");
    init_radiolib();

    if (lora_init_worker(&lora_do_on_work_cb, &lora_do_on_poll_cb, lora_log_prefix) != ROB_OK)
    {
        ROB_LOGE(lora_log_prefix, "Failed starting LoRa worker");
        return;
    }
    lora_set_queue_blocked(false);

    add_host_supported_media_type(robusto_mt_lora);

    ROB_LOGI(lora_log_prefix, "LoRa started.");
}

void robusto_lora_stop()
{
    ROB_LOGI(lora_log_prefix, "Stopping LoRa");
    // TODO: Something needed here?
    ROB_LOGI(lora_log_prefix, "LoRa started.");
}

/**
 * @brief Initialize LoRa
 *
 * @param _log_prefix

 */
void robusto_lora_init(char *_log_prefix)
{
    lora_log_prefix = _log_prefix;
    ROB_LOGI(lora_log_prefix, "Initializing LoRa");
#if CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH > -1
    ROB_LOGE("----", "LoRa KILL SWITCH ENABLED - GPIO %i", CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH);
    //robusto_gpio_set_direction(CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH, false);
    robusto_gpio_set_pullup(CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH, false);
    
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH) == true)
    {
        ROB_LOGE("----", "LoRa KILL SWITCH ON");
    } else {
        ROB_LOGE("----", "LoRa KILL SWITCH OFF");
    }
#endif

    init_lora_recover(lora_log_prefix);
    lora_messaging_init(lora_log_prefix);

    lora_peer_init(lora_log_prefix);
    ROB_LOGI(lora_log_prefix, "LoRa initialized");
}
#endif
