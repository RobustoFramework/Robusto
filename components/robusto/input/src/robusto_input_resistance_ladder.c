
#include <robusto_input.h>
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <robusto_logging.h>

#ifdef USE_ESPIDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#endif

SLIST_HEAD(resistor_ladder_head, resistor_ladder);
struct resistor_ladder_head ladder_head;

void monitor_ladder_cb();
void monitor_ladder_shutdown_cb();

static recurrence_t ladder_monitor = {
    recurrence_name : "Ladder monitor",
    skip_count : 0,
    skips_left : 0,
    recurrence_callback : &monitor_ladder_cb,
    shutdown_callback : &monitor_ladder_shutdown_cb
};

static char *input_log_prefix;


#ifdef USE_ESPIDF

void adc_calibration_init(adc_unit_t _adc_unit, adc_channel_t _adc_channel, adc_cali_handle_t *_cali_handle, adc_oneshot_unit_handle_t *_adc_handle)
{

    adc_cali_scheme_ver_t scheme_mask;

    ROB_LOGI(input_log_prefix, "Create ADC calibrations.");
    adc_cali_check_scheme(&scheme_mask);
    if (scheme_mask == ADC_CALI_SCHEME_VER_LINE_FITTING)
    {
        adc_cali_line_fitting_config_t line_fitting_config = {
            .unit_id = _adc_unit,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT};
        adc_cali_create_scheme_line_fitting(&line_fitting_config, _cali_handle);
    }
    else if (scheme_mask == ADC_CALI_SCHEME_VER_CURVE_FITTING)
    {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t curve_fitting_config = {
            .unit_id = adc_unit,
            .chan = adc_channel,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT};
        adc_cali_create_scheme_curve_fitting(&curve_fitting_config, cali_handle);
#else
        ROB_LOGE(input_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %s",
                 scheme_mask == 0 ? "Line" : "Curve");
#endif
    }
    else
    {
        ROB_LOGE(input_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %u", scheme_mask);
        return;
    }

    esp_err_t ret;

    ROB_LOGI(input_log_prefix, "Done Calibrating ADCs");
// TODO
// ADC1 config
#if !defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC)
    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = _adc_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE, ///< ADC controlled by ULP, see `adc_ulp_mode_t`
    };

    adc_oneshot_new_unit(&adc_init_cfg, _adc_handle);

    adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_11,         ///< ADC attenuation
        .bitwidth = ADC_BITWIDTH_DEFAULT, ///< ADC conversion result bits
    };

    adc_oneshot_config_channel(_adc_handle, _adc_channel, &adc_cfg);

#else
    // ADC2 config if applicable
    ESP_ERROR_CHECK(adc2_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc2_config_channel_atten(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL, ADC_ATTEN_DB_11));
#endif
}
#endif

bool match_single_resistor(uint32_t adc_voltage, resistance_mapping_t mapping)
{

    ROB_LOGD(input_log_prefix, "Comparing %lu to %u +/- %u", adc_voltage, mapping.adc_voltage, mapping.adc_stdev);
    if ((adc_voltage <= (mapping.adc_voltage + mapping.adc_stdev)) &&
        (adc_voltage >= (mapping.adc_voltage - mapping.adc_stdev)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

rob_ret_val_t robusto_input_test_resistor_ladder(double adc_voltage, resistor_ladder_t *ladder)
{
    if (!ladder)
    {
        return ROB_FAIL;
    }
    ROB_LOGI(input_log_prefix, "Looping %hu resistances", ladder->mapping_count);

    for (uint8_t curr_map = 1; curr_map < ladder->mapping_count; curr_map++)
    {

        if (match_single_resistor(adc_voltage, ladder->mappings[curr_map]))
        {
            ROB_LOGI(input_log_prefix, "Matched number %hu on %.1f to %u.", curr_map, adc_voltage, ladder->mappings[curr_map].adc_voltage);
            ladder->callback(curr_map);
            return ROB_OK;
        }
    }
    double resistance = (-(double)adc_voltage * (double)ladder->R1_resistance) / ((double)adc_voltage - (double)ladder->voltage);
    ROB_LOGD(input_log_prefix, "Not single press, calculated resistance : %.1f.", resistance);
    uint32_t curr_resistance;
    uint8_t matches = 0;

    for (uint8_t curr_map = 1; curr_map < ladder->mapping_count; curr_map++)
    {
        curr_resistance = ladder->mappings[curr_map].resistance;
        ROB_LOGD(input_log_prefix, "Resistance left %.1f curr res %lu curr stdev %u", resistance, curr_resistance, ladder->mappings[curr_map].adc_stdev);
        if (resistance >= (curr_resistance - ladder->mappings[curr_map].adc_stdev))
        {
            matches |= 1 << (curr_map - 1);
            resistance -= curr_resistance;
            ROB_LOGI(input_log_prefix, "Resistance included and subtracted %lu", curr_resistance);
        }
    }
    if (matches > 0)
    {
        ROB_LOGI(input_log_prefix, "Multiple buttons pressed, %hu", matches);
        ladder->callback(matches);
    }

    // TODO: Add the check on nothing pressed and if needed and the voltage is within resistance[0] range,
    // adjust ladder->voltage up with the appropriate amount
    return ROB_OK;
};

/**
 * @brief Add a monitor for a GPIO
 *
 */

rob_ret_val_t robusto_input_add_resistor_ladder(resistor_ladder_t *ladder)
{
    // TODO: Add checks on structure
    SLIST_INSERT_HEAD(&ladder_head, ladder, resistor_ladders);
    if (adc_oneshot_io_to_channel(ladder->GPIO, &ladder->adc_unit, &ladder->adc_channel) == ESP_OK)
    {
        adc_calibration_init(ladder->adc_unit, ladder->adc_channel, &ladder->cali_handle, &ladder->adc_handle);
    }
    return ROB_OK;
};

void monitor_ladder_cb()
{
    resistor_ladder_t *ladder;
    SLIST_FOREACH(ladder, &ladder_head, resistor_ladders)
    {

    }
}
void monitor_ladder_shutdown_cb()
{
}

/**
 * @brief Start resistance
 *
 */
void robusto_input_resistance_ladder_init(char *_input_log_prefix)
{
    input_log_prefix = _input_log_prefix;
    ROB_LOGI(input_log_prefix, "Input resistance ladder initiated.");
}
