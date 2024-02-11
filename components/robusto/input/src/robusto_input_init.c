/**
 * @file robusto_misc_init.c
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
#include <robusto_init_internal.h>
#ifdef CONFIG_ROBUSTO_INPUT
#include "robusto_input_init.h"
#include <robusto_init.h>

#include <robusto_input.h>


void robusto_input_stop() {
   
}

void robusto_input_start() {
    #ifdef CONFIG_ROBUSTO_INPUT
    robusto_input_resistance_monitor_start();
    #endif
    #ifdef CONFIG_ROBUSTO_INPUT_ADC_MONITOR
    robusto_input_start_adc_monitoring();
    #endif
}


void robusto_input_init(char * _log_prefix) {
    #ifdef CONFIG_ROBUSTO_INPUT
    robusto_input_resistance_monitor_init(_log_prefix);
    #endif
    #if defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR)
    robusto_input_init_adc_monitoring(_log_prefix);
    #endif

}



void register_input_service() {
    #if defined(CONFIG_ROBUSTO_INPUT)
    register_service(robusto_input_init, robusto_input_start, robusto_input_stop, 2, "Input");    
    #endif
}
#endif