#include "robusto_input_init.h"
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <robusto_system.h>
#include <inttypes.h>
#include <robusto_input.h>
#include <math.h>

#ifdef USE_ESPIDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#endif

static char *adc_monitor_log_prefix;

void monitor_adc_cb();
void monitor_adc_shutdown_cb();

recurrence_t memory_monitor = {
    recurrence_name : "ADC monitor",
    skip_count : 0,
    skips_left : 0,
    recurrence_callback : &monitor_adc_cb,
    shutdown_callback : &monitor_adc_shutdown_cb
};

#ifdef USE_ESPIDF

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

#endif
static adc_unit_t adc_unit;
static adc_channel_t adc_channel;
static adc_cali_handle_t cali_handle;
static adc_oneshot_unit_handle_t adc_handle;

#define SAMPLE_COUNT 20

bool cali_enabled = false;
uint8_t samples_collected = 0;
uint16_t samples[SAMPLE_COUNT];

typedef enum monitor_state
{
    ms_waiting,
    ms_sampling,
    ms_calculating,
    ms_printing,
    ms_done,

} monitor_state_t;

static monitor_state_t curr_state = ms_waiting;
resistance_mapping_t resistance_mappings[CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_LADDER_COUNT + 1];
uint8_t curr_mapping = 0;
uint16_t v1 = 0;

SLIST_HEAD(resistor_ladders, resistor_ladder)
resistor_ladders_t;

void monitor_adc_cb()
{
    uint32_t voltage = 0;
    if (curr_state == ms_waiting)
    {
        if (r_millis() > 3000)
        {
            curr_state = ms_sampling;
            samples_collected = 0;
            ROB_LOGI(adc_monitor_log_prefix, "ADC Done waiting, starts sampling.");
        }
    }
    else if (curr_state == ms_sampling)
    {
#ifdef USE_ESPIDF
        // Take two samples, quick succession, average
        int new_value1, new_value2;
        adc_oneshot_get_calibrated_result(adc_handle, cali_handle, adc_channel, &new_value1);
        adc_oneshot_get_calibrated_result(adc_handle, cali_handle, adc_channel, &new_value2);
        samples[samples_collected] = (new_value1 + new_value2) / 2;
#endif
        samples_collected++;
        if (samples_collected > SAMPLE_COUNT - 1)
        {
            curr_state = ms_calculating;
            ROB_LOGI(adc_monitor_log_prefix, "ADC Done sampling, starts calculating.");
        }
    }
    else if (curr_state == ms_calculating)
    {
        uint16_t highest = 0;
        uint16_t highest_pos = 0;
        uint16_t lowest = 4096;
        uint16_t lowest_pos = 0;
        uint16_t curr_value = 0;
        uint32_t total = 0;
        for (uint8_t curr_sample = 0; curr_sample < SAMPLE_COUNT; curr_sample++)
        {
            curr_value = samples[curr_sample];
            if (curr_value > highest)
            {

                highest = curr_value;
                highest_pos = curr_sample;
            }
            if (curr_value < lowest)
            {

                lowest = curr_value;
                lowest_pos = curr_sample;
            }
            total += curr_value;
        }
        // Find limits to produce the spread of the filtered data
        uint16_t highest_limit = 0;
        uint16_t lowest_limit = 4096;
        // TODO: Calculate the standard deviation instead and use that later instead.
        for (uint8_t curr_sample = 0; curr_sample < SAMPLE_COUNT; curr_sample++)
        {
            curr_value = samples[curr_sample];
            if (curr_value > highest_limit && highest_pos != curr_sample)
            {
                highest_limit = curr_value;
            }
            if (curr_value < lowest_limit && lowest_pos != curr_sample)
            {
                lowest_limit = curr_value;
            }
        }
        // Discard the lowest and highest values
        uint16_t mean_voltage = (total - lowest - highest) / (SAMPLE_COUNT - 2);
        double sum_of_squared_diff = 0.0;
        for (uint8_t curr_sample = 0; curr_sample < SAMPLE_COUNT; curr_sample++)
        {
            curr_value = samples[curr_sample];
            if (curr_sample != lowest_pos && curr_sample != highest_pos) {
                sum_of_squared_diff += pow(curr_value - mean_voltage, 2);
            }
        }
        double variance = sum_of_squared_diff / SAMPLE_COUNT;      
        double stdev = sqrt(variance);
        v1 = 3300;
        uint32_t resistance = (mean_voltage * CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_LADDER_R1) / (v1 - mean_voltage);
        ROB_LOGI(adc_monitor_log_prefix,
                 "cali average data: %" PRIu16 " mV, ADC - lowest: %" PRIu16 ", highest: %" PRIu16 ", average: %" PRIu16 ", stdev: %" PRIu16 ", highest_limit: %" PRIu16 ", lowest_limit: %" PRIu16,
                 mean_voltage, lowest, highest, mean_voltage,
                 highest_limit - lowest_limit, highest_limit, lowest_limit);
        ROB_LOGI(adc_monitor_log_prefix, "v1: %" PRIu16 " mV, Resistance: %" PRIu32, v1, resistance);

        resistance_mappings[curr_mapping].adc_stdev = highest_limit - lowest_limit;
        resistance_mappings[curr_mapping].adc_voltage = mean_voltage;
        if (curr_mapping > 0) {
            resistance_mappings[curr_mapping].resistance = resistance_mappings[0].resistance - resistance;
        } else {
            resistance_mappings[curr_mapping].resistance = resistance;
        }
        
        curr_mapping++;
        if (curr_mapping > CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_LADDER_COUNT)
        {
            curr_state = ms_printing;
            ROB_LOGI(adc_monitor_log_prefix, "ADC Done calculating, starts printing.");
            return;
        }
        ROB_LOGI(adc_monitor_log_prefix, "Please hold button %hu.", curr_mapping);
        r_delay(2000);
        ROB_LOGI(adc_monitor_log_prefix, "Starts collecting in 1 second.");
        r_delay(1000);
        ROB_LOGI(adc_monitor_log_prefix, "Collecting.");
        samples_collected = 0;
        curr_state = ms_sampling;
    }
    else if (curr_state == ms_printing)
    {

        char *buffer = robusto_malloc(1000);
        char *data = robusto_malloc(1000);

        uint16_t buffer_pos = sprintf(buffer, "C-code:\nresistance_mapping_t bm[%i] = {\n", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_LADDER_COUNT + 1);
        uint16_t data_pos = sprintf(data, "Data:\nresistance, adc_voltage, adc_stdev\n");
        for (uint8_t curr_sample = 0; curr_sample < CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_LADDER_COUNT + 1; curr_sample++)
        {
            buffer_pos += sprintf(buffer + buffer_pos, "    {.resistance = %" PRIu32 ", .adc_voltage = %" PRIu16 ", .adc_stdev = %" PRIu16 "},%s\n",
                                  resistance_mappings[curr_sample].resistance,
                                  resistance_mappings[curr_sample].adc_voltage,
                                  resistance_mappings[curr_sample].adc_stdev,
                                  curr_sample == 0 ? " //This is the total resistance, or base voltage value" : "");
            data_pos += sprintf(data + data_pos, "%" PRIu32 ",%" PRIu16 ",%" PRIu16 "\n",
                                resistance_mappings[curr_sample].resistance,
                                resistance_mappings[curr_sample].adc_voltage,
                                resistance_mappings[curr_sample].adc_stdev);
        }
        sprintf(buffer + buffer_pos, "}");
        ROB_LOGI(adc_monitor_log_prefix, "\n%.*s\n", buffer_pos + 1, buffer);
        ROB_LOGI(adc_monitor_log_prefix, "\n%.*s\n", data_pos + 1, data);
        robusto_free(buffer);
        robusto_free(data);
        curr_state = ms_done;
        ROB_LOGW(adc_monitor_log_prefix, "Done printing. To restart, reset.");
    }
}

void monitor_adc_shutdown_cb()
{
    ROB_LOGI(adc_monitor_log_prefix, "Stop!!");
};

/**
 * @brief Start monitoring
 *
 */
void robusto_input_start_adc_monitoring()
{
#ifdef CONFIG_ROBUSTO_INPUT_ADC_MONITOR
    ROB_LOGI(adc_monitor_log_prefix, "Starting ADC logging on Channel %i", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_GPIO);
    robusto_register_recurrence(&memory_monitor);
#endif
}

#ifdef USE_ESPIDF

void adc_calibration_init(void)
{

    adc_cali_scheme_ver_t scheme_mask;

    if (adc_oneshot_io_to_channel(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_GPIO, &adc_unit, &adc_channel) == ESP_OK)
    {

        ROB_LOGI(adc_monitor_log_prefix, "Create ADC calibrations.");
        adc_cali_check_scheme(&scheme_mask);
        if (scheme_mask == ADC_CALI_SCHEME_VER_LINE_FITTING)
        {
            adc_cali_line_fitting_config_t line_fitting_config = {
                .unit_id = adc_unit,
                .atten = ADC_ATTEN_DB_11,
                .bitwidth = ADC_BITWIDTH_DEFAULT};
            adc_cali_create_scheme_line_fitting(&line_fitting_config, &cali_handle);
        }
        else if (scheme_mask == ADC_CALI_SCHEME_VER_CURVE_FITTING)
        {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            adc_cali_curve_fitting_config_t curve_fitting_config = {
                .unit_id = adc_unit,
                .chan = adc_channel,
                .atten = ADC_ATTEN_DB_11,
                .bitwidth = ADC_BITWIDTH_DEFAULT};
            adc_cali_create_scheme_curve_fitting(&curve_fitting_config, &cali_handle);
#else
            ROB_LOGE(adc_monitor_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %s",
                     scheme_mask == 0 ? "Line" : "Curve");
#endif
        }
        else
        {
            ROB_LOGE(adc_monitor_log_prefix, "Error, unsupported calibration scheme returned by adc_cali_check_scheme: %u", scheme_mask);
            return;
        }
    }
    else
    {
        ROB_LOGE(adc_monitor_log_prefix, "Failed resolving GPIO channel");
        return;
    }
    esp_err_t ret;
    cali_enabled = false;
    ROB_LOGI(adc_monitor_log_prefix, "Done Calibrating ADCs");
// TODO
// ADC1 config
#if !defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC)
    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = adc_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE, ///< ADC controlled by ULP, see `adc_ulp_mode_t`
    };

    adc_oneshot_new_unit(&adc_init_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_11,         ///< ADC attenuation
        .bitwidth = ADC_BITWIDTH_DEFAULT, ///< ADC conversion result bits
    };

    adc_oneshot_config_channel(adc_handle, adc_channel, &adc_cfg);

#else
    // ADC2 config if applicable
    ESP_ERROR_CHECK(adc2_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc2_config_channel_atten(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL, ADC_ATTEN_DB_11));
#endif
}
#endif

void robusto_input_init_adc_monitoring(char *_log_prefix)
{
    adc_monitor_log_prefix = _log_prefix;
#ifdef USE_ESPIDF
    adc_calibration_init();
#else
#error "The input ADC monitor does not support the platform."
#endif
}