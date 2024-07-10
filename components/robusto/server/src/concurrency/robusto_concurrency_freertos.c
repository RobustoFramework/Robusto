/**
 * @file robusto_concurrency_freertos.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto concurrency implementation for freeRTOS-compatible platforms.
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
#include <robusto_concurrency.h>

#if defined(USE_ESPIDF) || defined(USE_ARDUINO)
#include <robusto_logging.h>
#ifdef USE_ARDUINO
#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#elif defined(USE_ESPIDF)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/projdefs.h>
#include "esp_task_wdt.h"
#endif

rob_ret_val_t robusto_create_task_custom(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity, uint32_t memory)  
{    
    #ifdef USE_ARDUINO // Arduino seems to only support one core
    int rc = xTaskCreate(task_function, task_name, memory, parameter, 8, handle);
    #else 
    int rc = xTaskCreatePinnedToCore(task_function, task_name, memory, parameter, 8, handle, affinity);
    #endif
    if (rc == pdTRUE)
    {
        return ROB_OK;
    } else {
        return ROB_ERR_INIT_FAIL;
    }
}

// TODO: This interface has to be finalized, the handle for example, and affinity
rob_ret_val_t robusto_create_task(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity)  
{
    return robusto_create_task_custom(task_function, parameter, task_name, handle, affinity, 8192);
}

rob_ret_val_t robusto_delete_current_task() {
    vTaskDelete(NULL);
    return ROB_OK;
}

mutex_ref_t robusto_mutex_init() {
    mutex_ref_t retval = xSemaphoreCreateMutex();
    if (!retval) {
        ROB_LOGE("FREERTOS", "Failure getting a semaphore");
    }
    return retval;
}

void robusto_mutex_deinit(mutex_ref_t mutex) {
    vSemaphoreDelete(mutex);
}

rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout) {
    if (pdTRUE == xSemaphoreTake(mutex, timeout/portTICK_PERIOD_MS)) {
        return ROB_OK;
    } else {
        return ROB_ERR_MUTEX;
    }
}

rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex) {

    if (pdTRUE == xSemaphoreGive(mutex)) {
        return ROB_OK;
    } else {
        return ROB_ERR_MUTEX;
    }
}

/**
 * @brief  If there is a task watchdog, set its timeout. 
 * @note This applies mostly to microcontrollers, has no effect on bigger computers(TODO: Maybe it should?)
 * @param watchdog_timeout Timeout in seconds
 */
void robusto_watchdog_set_timeout(int watchdog_timeout) {
    #ifdef USE_ARDUINO
    //wdt_disable(); //enable(watchdog_timeout);
    #endif

    #ifdef USE_ESPIDF
    // Adjust the ESP task watchdog, connections can take a long time sometimes.
    const esp_task_wdt_config_t watchdog_config = {
        .timeout_ms = (watchdog_timeout*1000) + 10,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
        .trigger_panic = false
    };
    esp_task_wdt_deinit();
    esp_task_wdt_init(&watchdog_config);
    #endif

}

void robusto_yield(void) {
    // TODO: This should be used instead of r_delay in approximately a million places. Plz fix.
#if defined(USE_ESPIDF)
    vTaskDelay(portTICK_PERIOD_MS);
#elif defined(ARDUINO_ARCH_RP2040)
    vTaskDelay(1);
#elif defined(ARDUINO_ARCH_STM32)    
    HAL_Delay(1);
#elif defined(ARDUINO_ARCH_MBED)
    wait_us(1);
#endif
}
    
#endif