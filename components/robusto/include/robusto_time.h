/**
 * @file robusto_time.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Time keeping functionality.
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

#pragma once

#include <robconfig.h>
#include <robusto_retval.h>
#include "sys/time.h"

#ifdef __cplusplus
extern "C"
{
#endif



/* Define all the reasonable times */
#define SECOND 1000000
#define MINUTE 60000000
#define HOUR 3600000000

/**
 * @brief Returns the number of milliseconds since boot (generalization of Arduino millis()) 
 * 
 * @return unsigned long 
 */
unsigned long r_millis();

/**
 * @brief Returns the number of microseconds since boot (generalization of Arduino millis()) 
 * 
 * @return unsigned long 
 */
unsigned long r_micros();

/**
 * @brief Waits for a number of milliseconds, if supported, yields control to other tasks
 * 
 * @param milliseconds 
 */
void r_delay(unsigned long milliseconds);

/**
 * @brief Waits for a number of microseconds, does not yield control to other tasks
 * 
 * @param milliseconds 
 */
void r_delay_microseconds(unsigned long microseconds);
/**
 * @brief Populates structure with time information (generalization of clang gettimeofday)
 * 
 * @param tv Timeval structur
 * @param tz Timezone
 * @return rob_ret_val_t 
 */
rob_ret_val_t r_gettimeofday(struct timeval *tv, struct timezone *tz);

/**
 * @brief Sets system time from structure with time information (generalization of standard settimeofday)
 * 
 * @param tv 
 * @param tz 
 * @return rob_ret_val_t 
 */
rob_ret_val_t r_settimeofday(const struct timeval *tv, const struct timezone * tz);

void r_init_time();

#ifdef __cplusplus
} /* extern "C" */
#endif