/**
 * @file main.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
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

#if defined(USE_ESPIDF)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#if defined(USE_ARDUINO)
#include <Arduino.h>
#include <robusto_concurrency.h>
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

#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_SERVER
#include "../hello/hello_service.h"
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI
#include "../ui/hello_ui.h"
#endif
#if defined(CONFIG_ROBUSTO_FLASH_EXAMPLE_SPIFFS) 
#include "../flash/flash.h"
#endif 
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_CLIENT
#include "../conductor/conductor_client.h"
#endif
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER
#include "../conductor/conductor_server.h"   
#endif
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
#include <robusto_conductor.h> 
#endif
#if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_GOOGLE_DRIVE)
#include "../umts/umts.h"   
#endif
#ifdef CONFIG_ROBUSTO_CAMERA_EXAMPLE
#include "../camera/camera.h"
#endif

#ifndef CONFIG_ROBUSTO_EXAMPLE_HELLO_CLIENT
#include <time.h>
#endif


#ifdef CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

char * example_log_prefix = "Example";

void setup_examples() {

    #ifdef CONFIG_HEAP_TRACING_STANDALONE
        ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
    #endif

    init_robusto();

    r_delay(100);   

    #ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI
    ROB_LOGI(example_log_prefix, "Start hello UI");
    start_hello_ui(example_log_prefix);
    #endif

    #ifdef CONFIG_ROBUSTO_FLASH_EXAMPLE_SPIFFS 
    ROB_LOGI(example_log_prefix, "Start flash example");
    init_flash_example(example_log_prefix);
    start_flash_example();
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
    #if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_GOOGLE_DRIVE)
    ROB_LOGI(example_log_prefix, "Start UMTS example");
    init_umts_example(example_log_prefix);
    start_umts_example();
    #endif
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_take_control();
    #endif  
    
    #ifdef CONFIG_ROBUSTO_CAMERA_EXAMPLE
    ROB_LOGI(example_log_prefix, "Start Camera example");
    init_camera_example(example_log_prefix);
    start_camera_example();
    #endif
}

void log_test()
{
    
    ROB_LOGI("Test_Tag", "Before delay %i", 1001);
    r_delay(1000);
    ROB_LOGI("Test_Tag", "After delay %i", 1002);
}

#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF))

/**
 * For native dev-platform or for some embedded frameworks
 */
int main(void)
{

    log_test();
}

#endif
void main_task(void *parameters)
{
    setup_examples();
    log_test();
    

    #ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_CLIENT
    
    while (1)
    {
        ROB_LOGI(example_log_prefix, "Calling HELLO server");
        hello_client_call_server();
        r_delay(7000);
    };
    
    #else
    uint32_t counter = 0;
    struct timeval tv;
    struct timezone tz;
    char datebuffer[80];
    struct tm *gm;

    while (1)
    {
        r_delay(1000);
        #ifndef CONFIG_ROBUSTO_INPUT_ADC_MONITOR
        r_gettimeofday(&tv, &tz);
        gm = gmtime(&tv.tv_sec);
        strftime(&datebuffer, 80, "%Y-%m-%d - %H:%M:%S", gm);
        ROB_LOGI_STAY("Example", "Current RTC time = %s:%li", datebuffer, tv.tv_usec % 1000);
        #endif
    };
    #endif
}


#ifdef USE_ESPIDF

/**
 * For ESP-IDF framework
 */
void app_main()
{
    main_task(NULL);
    
}

#endif

#ifdef USE_ARDUINO

/**
 * For the Arduino platform, here everything must manually run in a task if we are running freeRTOS.
 */


void setup()
{
    ROB_LOGI("Arduino", "Before creating task.");
    robusto_create_task((TaskFunction_t)&main_task, NULL, "Test task", NULL, 1);
}

void loop() {}

#endif
