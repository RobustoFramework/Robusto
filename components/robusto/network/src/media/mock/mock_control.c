/**
 * @file mock_control.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto mocking implementation
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
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "robusto_logging.h"
#include "mock_control.h"

#include "mock_queue.h"
#include "mock_messaging.h"
#include "mock_peer.h"
#include "mock_recover.h"


const char * mock_log_prefix;


void ROBUSTO_NETWORK_MOCK_TESTING_shutdown() {
    ROB_LOGE(mock_log_prefix, "Shutting down Mock. Why?");
    //TODO: Something needed here?
    ROB_LOGI(mock_log_prefix, "ESP-NOW shut down.");
}

/**
 * @brief Initialize Mock
 * 
 * @param _log_prefix 

 */
void ROBUSTO_NETWORK_MOCK_TESTING_init(char * _log_prefix) {

    mock_log_prefix = _log_prefix;
    ROB_LOGI(mock_log_prefix, "Initializing Mock");
    mock_peer_init(mock_log_prefix);
    init_mock_recover(mock_log_prefix);
    mock_messaging_init(mock_log_prefix);
    
    ROB_LOGI(mock_log_prefix, "Starting the mock worker.");
    if (mock_init_worker(&mock_do_on_work_cb, &mock_do_on_poll_cb, mock_log_prefix) != ROB_OK)
    {
       ROB_LOGE(mock_log_prefix, "Failed initializing mock worker!"); 
       return;
    }
    mock_set_queue_blocked(false);


    add_host_supported_media_type(robusto_mt_mock);
    ROB_LOGI(mock_log_prefix, "Mock initialized.");
}


#endif