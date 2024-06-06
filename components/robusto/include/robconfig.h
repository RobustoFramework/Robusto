/**
 * @file robconfig.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief This includes the Robusto config generated configuration files.
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

/* 
    This section selects between the Robusto use cases.
    The selection creates explicit defines so that Robusto can act more predictably.
*/

#if defined(ARDUINO) && !defined(ARDUINO_ARCH_ESP32)
/* Use the arduino framework. If we are not running the arduino-esp32, in which case we shouldn't. */
#define USE_ARDUINO 
#elif defined(ESP_PLATFORM)
/* Use the ESP-IDF platform */
#define USE_ESPIDF 
#elif defined(STM32)|| defined(ARDUINO_ARCH_STM32) || defined(ARDUINO_ARCH_MBED)
/* TODO: The STM32 platform is not properly defined, do that. It doesn't use USE_STM32, either, do that. */
#define USE_STM32 
#else
/* Not and MCU, it is native*/
#define USE_NATIVE

/* But we also need to know if it is windows */
#if defined(_WIN32) || defined(_WIN64)
#define USE_WINDOWS
#endif
#endif

// TODO: We need to get a proper fix of the environments to make proper choices; ESP-IDF, Arduino and STM32. And perhaps others.
// TODO: Consider what needs to be done WRT Zephyr, how would USE_ZEPHYR be added?

#ifdef USE_ESPIDF
#include <sdkconfig.h>
#else
#include "robconfig_.h"
#endif

#if defined(USE_ESPIDF) || defined(USE_ARDUINO) || defined(USE_STM32)
#define ROB_RTC_DATA_ATTR RTC_DATA_ATTR
#else 
#define ROB_RTC_DATA_ATTR
#endif