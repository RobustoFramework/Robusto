/**
 * @file robusto_concurrency.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Functionality for generic handling of concurrency; tasks/threads and their synchronization.
 * @note This is for the most normal cases, for more advanced variants, look into using the platform specific variants
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

#pragma once
#include <robconfig.h>

#include <stdint.h>
#include <stdbool.h>
#include <robusto_retval.h>
#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif
#ifdef USE_ARDUINO
#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#endif

// Tasks'n threads (FreeRTOS tasks are equated with POSIX threads here)
#if defined(USE_ESPIDF) || defined(USE_ARDUINO)
    typedef TaskHandle_t rob_task_handle_t;
    typedef SemaphoreHandle_t mutex_ref_t;
#else

#include <semaphore.h>
    typedef uint32_t rob_task_handle_t;

#ifndef USE_WINDOWS
    typedef pthread_mutex_t *mutex_ref_t;
#else
    #include <windows.h>
    typedef HANDLE mutex_ref_t;
#endif

#endif

#ifdef __cplusplus
extern "C"
{
#endif

    void init_robusto_concurrency(char * _log_prefix);

    // How all task/thread functions must look
    typedef void (*TaskFunction_t)(void *);

    /**
     * @brief Create a new task
     *
     * @param task_function The code to run
     * @param parameter A parameter that might be useful inside
     * @param task_name A name, useful for logging
     * @param affinity What core is preferred
     * @return uint32_t A handle to the task, 0 if failed
     */

    rob_ret_val_t robusto_create_task(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity);

    /**
     * @brief Create a new task
     *
     * @param task_function The code to run
     * @param parameter A parameter that might be useful inside
     * @param task_name A name, useful for logging
     * @param affinity What core is preferred
     * @param memory The amount of memory in bytes
     * @return uint32_t A handle to the task, 0 if failed
     */
    rob_ret_val_t robusto_create_task_custom(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity, uint32_t memory); 

    /**
     * @brief Delete the current task
     *
     * @return rob_ret_val_t Returns ROB_OK if successful
     */

    rob_ret_val_t robusto_delete_current_task();

    /* Semaphores */
    /**
     * @brief Initiate a new mutex
     *
     * @return mutex_ref_t A reference to the mutex
     */
    mutex_ref_t robusto_mutex_init();

    /**
     * @brief Initiate a new mutex
     *
     * @return mutex_ref_t A reference to the mutex
     */
    void robusto_mutex_deinit(mutex_ref_t mutex);

    /**
     * @brief Take a mutex
     *
     * @param mutex Pointer to the mutex
     * @param timeout_ms How long to wait until failure
     * @return rob_ret_val_t Returns ROB_OK if successful
     */
    rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout_ms);

    /**
     * @brief Give the mutex back
     *
     * @param mutex Pointer to the mutex
     * @return rob_ret_val_t Returns ROB_OK if successful
     */
    rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex);

    /**
     * @brief Set the watchdog timeout
     * @note This only applies to freeRTOS platforms
     *
     * @param watchdog_timeout How often the tash needs to call in using for example vTaskDelay()
     */
    void robusto_watchdog_set_timeout(int watchdog_timeout);

    /**
     * @brief Wait for a bool to become true
     *
     */
    bool robusto_waitfor_bool(bool *ptr, uint32_t timeout_ms);

    /**
     * @brief Wait for a byte to become some value
     *
     */
    bool robusto_waitfor_byte(uint8_t *byte_ptr, uint8_t value, uint32_t timeout_ms); 
    /**
     * @brief Wait for a byte to change value
     *
     */
    bool robusto_waitfor_byte_change(uint8_t *byte_ptr, uint32_t timeout_ms);

    /**
     * @brief Wait for a uint32_t to change value
     *
     */
    bool robusto_waitfor_uint32_t_change(uint32_t *uint32_t_ptr, uint32_t timeout_ms);

    /**
     * @brief Wait for a int to change value
     *
     */
    bool robusto_waitfor_int_change(int *int_ptr, uint32_t timeout_ms);
    /**
     * @brief Yields control to the scheduler
     *
     */
    void robusto_yield(void);



#ifdef __cplusplus
} /* extern "C" */
#endif

