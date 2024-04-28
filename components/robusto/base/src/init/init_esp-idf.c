/**
 * @file init_esp-idf.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto initialization for the ESP-IDF platform.
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

#include "robusto_init.h"
#ifdef USE_ESPIDF

#ifdef CONFIG_ROBUSTO_HEAP_TRACING
#include "esp_heap_trace.h"

#define NUM_RECORDS 200
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM

#endif
void robusto_init_compatibility()
{
#ifdef CONFIG_ROBUSTO_HEAP_TRACING
    #ifdef CONFIG_HEAP_TRACING_STANDALONE
        ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
    #else
    #error "Please select standalone heap tracing in menuconfig"
    #endif
#endif

}

#endif
