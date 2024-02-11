
/**
 * @file robusto_adc_monitoring.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ADC monitoring
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
#ifdef CONFIG_ROBUSTO_INPUT
#include <robusto_retval.h>
#ifdef USE_ESPIDF

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

// ADC Calibration
#if CONFIG_IDF_TARGET_ESP32
#define ADC_MONITOR_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_MONITOR_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_MONITOR_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_MONITOR_CALI_SCHEME ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif

rob_ret_val_t adc_calibration_init(adc_unit_t _adc_unit, adc_channel_t _adc_channel, adc_cali_handle_t *_cali_handle, adc_oneshot_unit_handle_t *_adc_handle);
#endif

/**
 * @brief Start monitoring resistance ladder
 * 
 */
void robusto_input_resistance_monitor_start();



/**
 * @brief Initialize the resistance ladder monitoring
 * 
 * @param _input_log_prefix 
 */
void robusto_input_resistance_monitor_init(char * _input_log_prefix);

/**
 * @brief Start monitoring an ADC and print out data
 * 
 */
void robusto_input_start_adc_monitoring();

/**
 * @brief Start monitoring an ADC and print out data
 * 
 */
void robusto_input_init_adc_monitoring(char * _log_prefix);

#endif