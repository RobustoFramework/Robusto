#include <robusto_input.h>
#ifdef CONFIG_ROBUSTO_INPUT_ADC_MONITOR 
#include "robusto_input_init.h"
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <robusto_system.h>
#include <inttypes.h>
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

static recurrence_t adc_monitor = {
    recurrence_name : "ADC monitor",
    skip_count : 0,
    skips_left : 0,
    recurrence_callback : &monitor_adc_cb,
    shutdown_callback : &monitor_adc_shutdown_cb
};

static adc_unit_t adc_unit;
static adc_channel_t adc_channel;
static adc_cali_handle_t cali_handle;
static adc_oneshot_unit_handle_t adc_handle;

#define SAMPLE_COUNT 20

bool cali_enabled = false;
uint8_t samples_collected = 0;
double samples[SAMPLE_COUNT];

typedef enum monitor_state
{
    ms_waiting,
    ms_sampling,
    ms_calculating,
    ms_printing,
    ms_done,

} monitor_state_t;

static monitor_state_t curr_state = ms_waiting;
resistance_mapping_t resistance_mappings[CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_COUNT + 1];
uint8_t curr_mapping = 0;
double v1 = 0;

void monitor_adc_cb()
{
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
        samples[samples_collected] = (double)(new_value1 + new_value2) / 2.0;
        // ROB_LOGI(adc_monitor_log_prefix, "%i%i", new_value1, new_value2);
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
        double highest = 0;
        double highest_pos = 0;
        double lowest = 4096;
        double lowest_pos = 0;
        double curr_value = 0;
        double total = 0;
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
        double highest_limit = 0;
        double lowest_limit = 4096;
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
        double mean_voltage = (double)(total - lowest - highest) / (double)(SAMPLE_COUNT - 2);
        double sum_of_squared_diff = 0.0;
        for (uint8_t curr_sample = 0; curr_sample < SAMPLE_COUNT; curr_sample++)
        {
            curr_value = samples[curr_sample];
            if (curr_sample != lowest_pos && curr_sample != highest_pos) {
                sum_of_squared_diff += (curr_value - mean_voltage) * (curr_value - mean_voltage);
            }
        }
        double variance = (double)sum_of_squared_diff / (double)SAMPLE_COUNT;      
        double stdev = sqrt(variance);
        v1 = CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_DIVIDER_V_IN;
        double resistance = (double)(mean_voltage * CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_DIVIDER_R1) / (double)(v1 - mean_voltage);
        ROB_LOGI(adc_monitor_log_prefix,
                 "cali average data: %1.f mV, ADC - lowest: %1.f, highest: %1.f, variance: %1.f stdev: %1.f, highest_limit: %1.f, lowest_limit: %1.f",
                 mean_voltage, lowest, highest,
                 variance, stdev, highest_limit, lowest_limit);
        ROB_LOGI(adc_monitor_log_prefix, "v1: %.1f mV, Resistance: %1.f", v1, resistance);

        resistance_mappings[curr_mapping].adc_stdev = stdev;
        resistance_mappings[curr_mapping].adc_voltage = mean_voltage;
        if (curr_mapping > 0) {
            resistance_mappings[curr_mapping].resistance = resistance_mappings[0].resistance - resistance;
        } else {
            resistance_mappings[curr_mapping].resistance = resistance;
        }
        
        curr_mapping++;
        if (curr_mapping > CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_COUNT)
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

        uint16_t buffer_pos = sprintf(buffer, "C-code:\nresistance_mapping_t bm[%i] = {\n", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_COUNT + 1);
        uint16_t data_pos = sprintf(data, "Data:\nresistance, adc_voltage, adc_stdev\n");
        for (uint8_t curr_sample = 0; curr_sample < CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTOR_COUNT + 1; curr_sample++)
        {
            buffer_pos += sprintf(buffer + buffer_pos, "    {.resistance = %.1f, .adc_voltage = %.1f, .adc_stdev = %.1f},%s\n",
                                  resistance_mappings[curr_sample].resistance,
                                  resistance_mappings[curr_sample].adc_voltage,
                                  resistance_mappings[curr_sample].adc_stdev,
                                  curr_sample == 0 ? " //This is the total resistance, or base source_voltage value" : "");
            data_pos += sprintf(data + data_pos, "%.1f, %.1f, %.1f\n",
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
    robusto_register_recurrence(&adc_monitor);
#endif
}

void robusto_input_init_adc_monitoring(char *_log_prefix)
{
    adc_monitor_log_prefix = _log_prefix;
#ifdef USE_ESPIDF
    if (adc_oneshot_io_to_channel(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_GPIO, &adc_unit, &adc_channel) == ESP_OK)
    {
        adc_calibration_init(adc_unit, adc_channel, &cali_handle, &adc_handle);
    }
#else
#error "The input ADC monitor does not support the platform."
#endif
}
#endif