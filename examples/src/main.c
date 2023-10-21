/**
 * @file main.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Main entry point for the Robusto project.
 * @note This file is not supposed to be executed, but a part of the development functionality.
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
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

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#if defined(ARDUINO)
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>
#endif

#if 0
#ifdef BAREMETAL
#include <Arduino_FreeRTOS.h>
#endif
#endif

#include <string.h>

#include <robusto_logging.h>
#include <robusto_time.h>
#include <robusto_init.h>
#include <robusto_network_init.h>
#include <robusto_server_init.h>
#include <robusto_misc_init.h>
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_SERVER
#include "../hello/hello_service.h"
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI
#include "../ui/hello_ui.h"
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_CLIENT
#include "../conductor/conductor_client.h"
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER 
#include "../conductor/conductor_server.h"   
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_SMS
#include "../sms/sms.h"   
#endif
#ifdef CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

char * example_log_prefix = "Example";

void setup() {

    #ifdef CONFIG_HEAP_TRACING_STANDALONE
        ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
    #endif
    register_network_service();
    register_server_service();

    // TODO: Create a pubsub example
    #ifdef CONFIG_ROBUSTO_EXAMPLE_PUBSUB_SERVER 
    register_misc_service();

    #endif


    init_robusto();

    r_delay(100);   

    #ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI
    ROB_LOGI(example_log_prefix, "Start hello UI");
    start_hello_ui(example_log_prefix);
    #endif

    #if CONFIG_ROBUSTO_EXAMPLE_HELLO_SERVER
    #ifndef CONFIG_ROBUSTO_EXAMPLE_HELLO_CLIENT
    ROB_LOGI(example_log_prefix, "Start hello example server");
    init_hello_service(example_log_prefix);
    #else 
    ROB_LOGI(example_log_prefix, "Start hello example client");
    init_hello_client(example_log_prefix);
    #endif
    #endif
    #ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_CLIENT
    ROB_LOGI(example_log_prefix, "Start example conductor client");
    init_conductor_client(example_log_prefix);
    conductor_client_call_server();
    #endif

    #ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER 
    ROB_LOGI(example_log_prefix, "Start example conductor server");
    init_conductor_server(example_log_prefix);
    run_conductor_server();
    #endif
    #ifdef CONFIG_ROBUSTO_EXAMPLE_SMS
    ROB_LOGI(example_log_prefix, "Start SMS examples");
    init_sms_example(example_log_prefix);
    start_sms_example();
    #endif
}

void log_test()
{
    
    ROB_LOGI("Test_Tag", "Before delay %i", 1001);
    r_delay(1000);
    ROB_LOGI("Test_Tag", "After delay %i", 1002);
}

#if !(defined(ARDUINO) || defined(ESP_PLATFORM))

/**
 * For native dev-platform or for some embedded frameworks
 */
int main(void)
{

    log_test();
}

#endif

#ifdef ESP_PLATFORM

/**
 * For ESP-IDF framework
 */
void app_main()
{
    setup();
    log_test();


    #ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_CLIENT
    
    while (1)
    {
        ROB_LOGI(example_log_prefix, "Calling HELLO server");
        hello_client_call_server();
        r_delay(7000);
    };
    
    #else
    while (1)
    {
        r_delay(100);
    };
    #endif
    // while(1) {r_delay(100);};
    
}

#endif

#ifdef ARDUINO

/**
 * For the Arduino platform, here everything must manually run in a task if we are running freeRTOS.
 */

void main_task(void *parameters)
{
    init_robusto();
    ROB_LOGI("Arduino", "Before creating task.");
    delay(2000);
    log_test();
    vTaskDelete(NULL);
}

void setup()
{
    xTaskCreate((TaskFunction_t)&main_task, "Test task", 128, NULL, 1, NULL);
}

void loop() {}

#endif
