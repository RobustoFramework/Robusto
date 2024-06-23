
#include <robusto_input.h>
#ifdef CONFIG_ROBUSTO_INPUT
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <robusto_logging.h>

#ifdef USE_ESPIDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#endif



SLIST_HEAD(resistor_monitors_head, resistor_monitor);
struct resistor_monitors_head monitors_head;

static void resistance_monitor_cb();
static void resistance_monitor_shutdown_cb();

static recurrence_t resistance_monitor = {
    recurrence_name : "Resistance monitor",
    skip_count : 0,
    skips_left : 0,
    recurrence_callback : &resistance_monitor_cb,
    shutdown_callback : &resistance_monitor_shutdown_cb
};

static char *input_log_prefix;

#ifdef USE_ESPIDF

rob_ret_val_t adc_calibration_init(adc_unit_t _adc_unit, adc_channel_t _adc_channel, adc_cali_handle_t *_cali_handle, adc_oneshot_unit_handle_t *_adc_handle)
{
    ROB_LOGI(input_log_prefix, "Create ADC calibrations for ADC unit %i and channel %i.", _adc_unit, _adc_channel);
    adc_cali_scheme_ver_t scheme_mask;
    esp_err_t sch_res = adc_cali_check_scheme(&scheme_mask);
    if (sch_res != ESP_OK)
    {
        ROB_LOGE(input_log_prefix, "adc_cali_check_scheme failed with the %i error code.", sch_res);
        return ROB_FAIL;
    }
    if (scheme_mask == ADC_CALI_SCHEME_VER_LINE_FITTING)
    {
        adc_cali_line_fitting_config_t line_fitting_config = {
            .unit_id = _adc_unit,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT};
        esp_err_t lf_res = adc_cali_create_scheme_line_fitting(&line_fitting_config, _cali_handle);
        if (lf_res != ESP_OK)
        {
            ROB_LOGE(input_log_prefix, "adc_cali_create_scheme_line_fitting failed with the %i error code.", lf_res);
            return ROB_FAIL;
        }
    }
    else if (scheme_mask == ADC_CALI_SCHEME_VER_CURVE_FITTING)
    {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t curve_fitting_config = {
            .unit_id = adc_unit,
            .chan = adc_channel,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT};
        esp_err_t cf_res = adc_cali_create_scheme_curve_fitting(&curve_fitting_config, cali_handle);
        if (cf_res != ESP_OK)
        {
            ROB_LOGE(input_log_prefix, "adc_cali_create_scheme_curve_fitting failed with the %i error code.", cf_res);
        }
#else
        ROB_LOGE(input_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %s",
                 scheme_mask == 0 ? "Line" : "Curve");
#endif
    }
    else
    {
        ROB_LOGE(input_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %u", scheme_mask);
        return ROB_FAIL;
    }

    ROB_LOGI(input_log_prefix, "Done Calibrating ADCs");

// ADC1 config
#if !defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC)
    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = _adc_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE, ///< ADC controlled by ULP, see `adc_ulp_mode_t`
    };

    esp_err_t new_res = adc_oneshot_new_unit(&adc_init_cfg, _adc_handle);
    if (new_res != ESP_OK)
    {
        ROB_LOGE(input_log_prefix, "adc_oneshot_new_unit failed with the %i error code.", new_res);
    }
    adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_11,         ///< ADC attenuation
        .bitwidth = ADC_BITWIDTH_DEFAULT, ///< ADC conversion result bits
    };

    esp_err_t ch_res = adc_oneshot_config_channel(*_adc_handle, _adc_channel, &adc_cfg);
    if (ch_res != ESP_OK)
    {
        ROB_LOGE(input_log_prefix, "adc_oneshot_new_unit failed with the %i error code.", ch_res);
        return ROB_FAIL;
    }
#else
    // ADC2 config if applicable
    ESP_ERROR_CHECK(adc2_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc2_config_channel_atten(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL, ADC_ATTEN_DB_11));
#endif
    return ROB_OK;
}
#endif

bool match_single_resistor(double adc_voltage, resistance_mapping_t mapping)
{

    ROB_LOGD(input_log_prefix, "Comparing %.1f to %.1f +/- %.1f", adc_voltage, mapping.adc_voltage, mapping.adc_stdev);
    if ((adc_voltage <= (mapping.adc_voltage + (mapping.adc_stdev * 2))) &&
        (adc_voltage >= (mapping.adc_voltage - (mapping.adc_stdev * 2))))
    {
        return true;
    }
    else
    {
        return false;
    }
}
void handle_matches(uint32_t matches, float voltage, resistor_monitor_t *monitor)
{
    if (matches != monitor->matches)
    {
        monitor->matches = matches;
        if (monitor->callback)
        {
            monitor->callback(matches, voltage);
        }
    }
}

rob_ret_val_t match_multiple_resistors(double adc_voltage, resistor_monitor_t *monitor)
{

    uint8_t matches = 0;
    double calc_voltage = 0;
    double curr_resistance = 0;
    uint8_t last_match, match_count = 0;
    // We start from top, we begin by assuming that no button has been pressed, We also discount the check resistor.
    double subtr_resistance = monitor->mappings[0].resistance + monitor->R2_check_resistor;
    for (uint8_t curr_map = 1; curr_map < monitor->mapping_count - monitor->ladder_exclude_count; curr_map++)
    {
        curr_resistance = monitor->mappings[curr_map].resistance;

        calc_voltage = (double)(monitor->source_voltage * (double)(subtr_resistance - curr_resistance)) /
                       (double)(monitor->R1 + subtr_resistance - curr_resistance);
        ROB_LOGD(input_log_prefix, "Testing for resistor %hu, curr accu %1.f res %1.f curr stdev %.1f, adc_voltage %.1f, calc_voltage %.1f",
                 curr_map - 1, subtr_resistance, curr_resistance, monitor->mappings[curr_map].adc_stdev, adc_voltage, calc_voltage);

        if ((double)adc_voltage <= (calc_voltage + (monitor->mappings[curr_map].adc_stdev * 2)))
        {
            matches |= 1 << (curr_map - 1);
            last_match = curr_map;
            match_count++;
            subtr_resistance -= curr_resistance;
            ROB_LOGD(input_log_prefix, "Match. accu %1.f, curr %1.f", subtr_resistance, curr_resistance);
        }
    }
    // Check the lower bound so that the last wasn't included erroneously
    if ((matches > 0) && ((double)adc_voltage <= (calc_voltage - (monitor->mappings[last_match].adc_stdev * 4))))
    {
        matches &= ~(1 << (last_match - 1));
        match_count++;
    }

    // If there is only one match, it should have been identified in the single check, it is likely a spike
    if (match_count < 2)
    {
        return ROB_OK;
    }

    if (matches > 0)
    {
        ROB_LOGD(input_log_prefix, "Multiple resistors bypassed(buttons pressed), value: %hu", matches);
        handle_matches(matches, adc_voltage, monitor);
    }
    return ROB_OK;
}

rob_ret_val_t robusto_input_check_resistor_monitor(resistor_monitor_t *monitor)
{

    if (!monitor)
    {
        ROB_LOGE(input_log_prefix, "robusto_input_check_resistor_monitor - monitor is NULL!");
        return ROB_FAIL;
    }
    double adc_voltage = 0;
    double R2 = 0;
    double first_voltage = -1;
    // We do this check twice to avoid reacting to dips
    for (int count = 0; count < 2; count++)
    {

        // Take two samples, quick succession, average
        int value1, value2;
#ifdef USE_ESPIDF
        adc_oneshot_get_calibrated_result(monitor->adc_handle, monitor->cali_handle, monitor->adc_channel, &value1);
        adc_oneshot_get_calibrated_result(monitor->adc_handle, monitor->cali_handle, monitor->adc_channel, &value2);
#endif
        if (value2 < value1 - CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE || value2 > value1 + CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE)
        {
            // Filter large movements
            ROB_LOGD(input_log_prefix, "Filter out movements > %u mV %i", CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE, (int)value1 - (int)value2);
            return ROB_OK;
        }
        adc_voltage = (double)(value1 + value2) / 2.0;

        if ((first_voltage > 0) && ((first_voltage < adc_voltage - CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE) || (first_voltage > adc_voltage + CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE)))
        {
            ROB_LOGD(input_log_prefix, "Filter out > %u mv change between first and second average  %1.f", CONFIG_ROBUSTO_INPUT_MOVEMENT_TOLERANCE, first_voltage - adc_voltage);
            return ROB_OK;
        }
        else
        {
            first_voltage = adc_voltage;
        }

        if ((monitor->R2_check_resistor > 0) && (adc_voltage < CONFIG_ROBUSTO_INPUT_MINIMUM_VOLTAGE))
        {
            ROB_LOGE(input_log_prefix, "Voltage beneath %u mV, possible short or disconnect!", CONFIG_ROBUSTO_INPUT_MINIMUM_VOLTAGE);
            // TODO: Handle failure in some way
            return ROB_FAIL;
        }

        R2 = (-(double)adc_voltage * (double)monitor->R1) / ((double)adc_voltage - (double)monitor->source_voltage);
        if (match_single_resistor(adc_voltage, monitor->mappings[0]))
        {
            handle_matches(0, adc_voltage, monitor);

            ROB_LOGD(input_log_prefix, "At reference voltage %.1f and R2 %.1f, no match.", adc_voltage, R2);
            // TODO: Update V1 if needed.
            return ROB_OK;
        }
        else
        {
            r_delay(5);
        }
    }
    ROB_LOGD(input_log_prefix, "Looping %hu resistances, ADC voltage: %.1f, calculated R2: %.1f.", monitor->mapping_count, adc_voltage, monitor->mappings[0].resistance - R2);

    uint8_t matches = 0;
    for (uint8_t curr_map = 1; curr_map < monitor->mapping_count; curr_map++)
    {

        if (match_single_resistor(adc_voltage, monitor->mappings[curr_map]))
        {
            ROB_LOGI(input_log_prefix, "Matched number %hu on %.1f to %.1f.", curr_map, adc_voltage, monitor->mappings[curr_map].adc_voltage);
            matches |= 1 << (curr_map - 1);
            handle_matches(matches, adc_voltage, monitor);
            return ROB_OK;
        }
    }
    if (!monitor->ladder_decode)
    {
        return ROB_OK;
    }

    return match_multiple_resistors(adc_voltage, monitor);
    // TODO: Add the check on nothing pressed and if needed and the voltage is within resistance[0] range,
    // adjust monitor->voltage up with the appropriate amount
};

/**
 * @brief Add a monitor for a GPIO
 *
 */

rob_ret_val_t robusto_input_add_resistor_monitor(resistor_monitor_t *monitor)
{
    // TODO: Add checks on structure
    if (adc_oneshot_io_to_channel(monitor->GPIO, &monitor->adc_unit, &monitor->adc_channel) == ESP_OK)
    {
        if (adc_calibration_init(monitor->adc_unit, monitor->adc_channel, &monitor->cali_handle, &monitor->adc_handle) != ESP_OK)
        {
            ROB_LOGE(input_log_prefix, "adc_oneshot_new_unit failed, will not add monitor.");
            return ROB_FAIL;
        }
        else
        {
            SLIST_INSERT_HEAD(&monitors_head, monitor, resistor_monitors);
            ROB_LOGI(input_log_prefix, "The monitor \"%s\" on GPIO %hu, ADC unit %i, channel %i, cali handle %p, adc_handle %p got initiated and added",
                     monitor->name, monitor->GPIO, monitor->adc_unit, monitor->adc_channel, monitor->cali_handle, monitor->adc_handle);
            return ROB_OK;
        }
    }
    else
    {
        ROB_LOGE(input_log_prefix, "The monitor GPIO %hu could not be translated into an ADC unit and channel.", monitor->GPIO);
        return ROB_FAIL;
    }
};

void resistance_monitor_cb()
{

    resistor_monitor_t *monitor;
    SLIST_FOREACH(monitor, &monitors_head, resistor_monitors)
    {
        robusto_input_check_resistor_monitor(monitor);
    }
}
void resistance_monitor_shutdown_cb()
{
}

/**
 * @brief Start monitoring
 *
 */
void robusto_input_resistance_monitor_start()
{
#if defined(CONFIG_ROBUSTO_INPUT) && !defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR)
    ROB_LOGI(input_log_prefix, "Starting ADC monitoring.");
    robusto_register_recurrence(&resistance_monitor);
#endif
}

/**
 * @brief Start resistance
 *
 */
void robusto_input_resistance_monitor_init(char *_input_log_prefix)
{
    input_log_prefix = _input_log_prefix;
    SLIST_INIT(&monitors_head);
    ROB_LOGI(input_log_prefix, "Input resistance monitor initiated.");
}
#endif
