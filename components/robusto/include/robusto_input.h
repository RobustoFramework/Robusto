
/**
 * @file robusto_input.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief User input management
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

#include <robusto_retval.h>
#include <stdbool.h>

#ifndef USE_ARDUINO
#include "sys/queue.h"
#else
#include <compat/arduino_sys_queue.h>
#endif

#ifdef USE_ESPIDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#endif

/**
 * @brief Callback for 
 * 
 */
typedef void(cb_buttons_press)(uint32_t buttons);

typedef struct resistance_mapping {
    /* The value of the resistor */
    uint32_t resistance;
    /* The value of the adc */
    uint16_t adc_voltage;
    /* The acceptable width */
    uint16_t adc_stdev;

} resistance_mapping_t;



/**
 * @brief The button ladder collects buttons connected to resistors
 * @note The first resistance ladder is without buttons/connections per convention
 */
typedef struct resistor_ladder {
    /* Mappings of resistances (first is without connections) */
    resistance_mapping_t *mappings;

    /* Number of mappings */
    uint8_t mapping_count;
    /* The GPIO to monitor */
    uint8_t GPIO;
    /* Callback */
    cb_buttons_press * callback;
    /* Voltage divider R1 - needed to keep voltage down into ADC range */
    uint32_t R1_resistance;
    /* Voltage */
    uint32_t voltage;
    /* If it is a serial or parallell, default false */
    bool parallel;

    #ifdef USE_ESPIDF
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    adc_cali_handle_t cali_handle;
    adc_oneshot_unit_handle_t adc_handle;
    #endif

    SLIST_ENTRY(resistor_ladder) resistor_ladders; /* Singly linked list */
} resistor_ladder_t;

/**
 * @brief Add a resistor ladder to the list of ladders to monitor
 * 
 * @param ladder 
 * @return rob_ret_val_t 
 */
rob_ret_val_t robusto_input_add_resistor_ladder(resistor_ladder_t * ladder);

rob_ret_val_t robusto_input_test_resistor_ladder(double adc_val, resistor_ladder_t * ladder);

