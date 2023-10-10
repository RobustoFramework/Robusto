/**
 * @file lora.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief LoRa (from "long range") is a physical proprietary radio communication technique.
 * See wikipedia: https://en.wikipedia.org/wiki/LoRa
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
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

#ifdef CONFIG_RADIOLIB_DEBUG
    #define RADIOLIB_DEBUG
    #define RADIOLIB_DEBUG_PRINT(...) ROB_LOGI("RadioLibDebug",  __VA_ARGS__);
    #define RADIOLIB_DEBUG_PRINTLN RADIOLIB_DEBUG_PRINT
#endif
#ifdef __cplusplus
extern "C"
{
#endif
void robusto_lora_start();
void robusto_lora_stop();

void robusto_lora_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif