/**
 * @file time_arduino.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto time functionality for Arduino
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

#include <robconfig.h>
#include <robusto_time.h>
#ifdef USE_ARDUINO
#include <Arduino.h>

// TODO: Remove synchronous mode
#ifdef ARDUINO_ARCH_RP2040
    #include <FreeRTOS.h>
    #include <task.h>
#endif

/**
 * @brief 
 * @note 
 * @return long long
 */
unsigned long r_millis()
{
    return millis();
}
unsigned long r_micros()
{
    return micros();
}

void r_delay(unsigned long milliseconds)
{
    #ifdef ARDUINO_ARCH_RP2040
    vTaskDelay(milliseconds/portTICK_PERIOD_MS);
    #else
    delay(milliseconds);
    #endif
}
void r_delay_microseconds(unsigned long microseconds)
{
    delayMicroseconds(microseconds);
}

void r_init_time(){};

#endif