/**
 * @file robusto_concurrency_native.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto concurrency implementation for native-compatible platforms (Linux/Windows/MacOS).
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
#if !(defined(USE_ESPIDF) || defined(USE_ARDUINO))

#include <robusto_time.h>
#include <robusto_retval.h>

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdlib.h> 

rob_ret_val_t robusto_create_task(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity) {
    pthread_t new_thread;
    int rc = pthread_create(&new_thread, NULL, (void *)task_function, parameter);
    if (rc == ROB_OK) {
        return ROB_OK;
    } else {
        return ROB_ERR_INIT_FAIL;
    }
}


rob_ret_val_t robusto_delete_current_task()  {
    pthread_exit(NULL);
    return ROB_OK;    
}

mutex_ref_t robusto_mutex_init() {
    pthread_mutex_t* lock = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(lock, NULL); 
    return lock;
;
}

void robusto_mutex_deinit(mutex_ref_t mutex) {
    pthread_mutex_destroy(mutex);
}

rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout) {
    int start_time = r_millis();  
    while (r_millis() < (start_time + timeout)) {
        ;
        if (ROB_OK == pthread_mutex_trylock(mutex)) {
            return ROB_OK;
        }
        r_delay(1);
    }
    return ROB_ERR_MUTEX;
}

rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex) {

    if (ROB_OK == pthread_mutex_unlock(mutex)) {
        return ROB_OK;
    } else {
        return ROB_ERR_MUTEX;
    }

}

/**
 * @brief  If there is a task watchdog, set its timeout
 * @note This applies mostly to microcontrollers, has no effect on bigger computers (TODO: Maybe it should?)
 * @param watchdog_timeout Timeout in seconds
 */
void robusto_watchdog_set_timeout(int watchdog_timeout) {

}
    

void robusto_yield(void) {
    sched_yield();
}

#endif