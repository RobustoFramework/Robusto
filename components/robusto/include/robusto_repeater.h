/**
 * @file robusto_repeater.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Base repeating functionality 
 * @version 0.1
 * @date 2023-07-28
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
 * SUBSTITUTE GOODS OR recurrenceS; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robconfig.h>

#pragma once

#include <inttypes.h>
#include <robusto_retval.h>

#ifdef __cplusplus
extern "C"
{
#endif


// Shortcust to always getting at least the delay wanted regardles of base tick delay
#define ROBUSTO_MONITOR_1_SEC 1000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
#define ROBUSTO_MONITOR_2_SEC 2000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
#define ROBUSTO_MONITOR_30_SEC 30000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
#define ROBUSTO_MONITOR_1_MIN 60000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
#define ROBUSTO_MONITOR_2_MIN 120000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
#define ROBUSTO_MONITOR_10_MIN 1200000/CONFIG_ROBUSTO_MONITOR_BASE_TICK_DELAY_MS
// The recurrences callback
typedef void(recurrence_cb)(void);
// recurrence shutdown callback
typedef void(shutdown_cb)(void);

/**
 * @brief The recurrence structure contains information that the recurrence wants to provide
 * Freed by the recurrence itself on shutdown. 
 */
typedef struct recurrence
{
    // The name of the recurrence
    char * recurrence_name;
    // Skip this many calls before running
    uint16_t skip_count;   
    // Calls left to skip
    uint16_t skips_left;       
    // The callback for what to do
    recurrence_cb * recurrence_callback;
    // The callback for shutting down the recurrence
    shutdown_cb * shutdown_callback;
} recurrence_t;

/**
 * @brief Force runnig all the repeaters immidiately, normally not needed, used to speed up testing.
 * 
 */
void run_all_repeaters_now();

/**
 * @brief Register a recurrence 
 * 
 * @param recurrence A struct containing all important information
 */
rob_ret_val_t robusto_register_recurrence(recurrence_t * recurrence);
/**
 * @brief Shutdown recurrences
 * 
 */

void robusto_repeater_shutdown();

#ifdef __cplusplus
} /* extern "C" */
#endif
