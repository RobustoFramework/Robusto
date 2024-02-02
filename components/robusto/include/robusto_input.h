
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

/**
 * @brief Callback for 
 * 
 */
typedef void(cb_buttons_press)(uint32_t buttons);

typedef struct resistance_mapping {
    /* The value of the resistor */
    uint32_t resistance;
    /* The value of the adc */
    uint16_t adc_value;
    /* The acceptable width */
    uint16_t adc_spread;
} resistance_mapping_t;

/**
 * @brief The button map collects buttons connected to resistors
 */
typedef struct resistance_button {
    /* The mappings*/
    resistance_mapping_t **mappings;
    /* Normal voltage, used to make the solution insensitive to voltage fluctuations */
    float vcc_normal;
    /* The GPIO to monitor */
    uint8_t gpio_num;
    /* Callback */
    cb_buttons_press * callback;
    /* Voltage divider R1 - needed to keep voltage down into ADC range */
    uint32_t R1_resistance;
} button_map_t;

rob_ret_val_t robusto_add_bit_ladder_button(button_map_t * map);

