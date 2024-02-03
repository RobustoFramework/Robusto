#include "robusto_input_init.h"
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <robusto_system.h>
#include <inttypes.h>
#include <robusto_input.h>
#ifdef USE_ESPIDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#endif

static char *adc_monitor_log_prefix;

void monitor_adc_cb();
void monitor_adc_shutdown_cb();

static char _monitor_name[16] = "Adc monitor\x00";

recurrence_t memory_monitor = {
    recurrence_name : &_monitor_name,
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

static esp_adc_cal_characteristics_t adc1_chars;

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
resistance_mapping_t resistance_mappings[CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTANCE_COUNT + 1];
uint8_t curr_mapping = 0;
uint16_t v1 = 0;

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
        samples[samples_collected] = (adc1_get_raw(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL) + adc1_get_raw(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL)) / 2;
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
        uint16_t average = (total - lowest - highest) / (SAMPLE_COUNT - 2);
        v1 = 3300;
        uint32_t resistance = 0;
        if (cali_enabled)
        {
            voltage = esp_adc_cal_raw_to_voltage(average, &adc1_chars);
            /*
            for (uint8_t curr_sample2 = 0; curr_sample2 < SAMPLE_COUNT; curr_sample2++)
            {
                ROB_LOGI(adc_monitor_log_prefix, "Data point: %" PRIu16, samples[curr_sample2]);
            }
            */
            resistance = (voltage * CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_R1) / (v1 - voltage);
            ROB_LOGI(adc_monitor_log_prefix,
                     "cali average data: %" PRIu32 " mV, ADC - lowest: %" PRIu16 ", highest: %" PRIu16 ", average: %" PRIu16 ", spread: %" PRIu16 ", highest_limit: %" PRIu16 ", lowest_limit: %" PRIu16,
                     voltage, lowest, highest, average,
                     highest_limit - lowest_limit, highest_limit, lowest_limit);
            ROB_LOGI(adc_monitor_log_prefix, "v1: %" PRIu16 " mV, Resistance: %" PRIu32, v1, resistance);
        }

        resistance_mappings[curr_mapping].adc_spread = highest_limit - lowest_limit;
        resistance_mappings[curr_mapping].adc_value = average;
        resistance_mappings[curr_mapping].resistance = resistance;
        curr_mapping++;
        if (curr_mapping > CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTANCE_COUNT)
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
        
        char * buffer = robusto_malloc(1000);
        char * data = robusto_malloc(1000);
        
        uint16_t buffer_pos = sprintf(buffer, "C-code:\nresistance_mapping_t bm[%i] = {\n", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTANCE_COUNT + 1);
        uint16_t data_pos = sprintf(data, "Data:\nresistance, adc_value, adc_spread\n");
        for (uint8_t curr_sample = 0; curr_sample < CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTANCE_COUNT + 1; curr_sample++)
        {
            buffer_pos+= sprintf(buffer + buffer_pos, "    {.resistance = %" PRIu32 ", .adc_value = %" PRIu16 ", .adc_spread = %" PRIu16 "},%s\n",
                     resistance_mappings[curr_sample].resistance, 
                     resistance_mappings[curr_sample].adc_value, 
                     resistance_mappings[curr_sample].adc_spread, 
                     curr_sample == 0 ? " //This is the total resistance, or base voltage value": "");
            data_pos+= sprintf(data + data_pos, "%" PRIu32 ",%" PRIu16 ",%" PRIu16 "\n",
                     resistance_mappings[curr_sample].resistance, 
                     resistance_mappings[curr_sample].adc_value, 
                     resistance_mappings[curr_sample].adc_spread);

        }
        sprintf(buffer + buffer_pos,"}");
        ROB_LOGI(adc_monitor_log_prefix, "\n%.*s\n", buffer_pos+1, buffer);
        ROB_LOGI(adc_monitor_log_prefix, "\n%.*s\n", data_pos+1, data);
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

    ROB_LOGI(adc_monitor_log_prefix, "Starting ADC logging on Channel %i", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL);
    robusto_register_recurrence(&memory_monitor);
}

#ifdef USE_ESPIDF

void adc_calibration_init(void)
{
    esp_err_t ret;
    cali_enabled = false;
    ROB_LOGI(adc_monitor_log_prefix, "Calibrating ADCs");
    ret = esp_adc_cal_check_efuse(ADC_MONITOR_CALI_SCHEME);
    if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        ROB_LOGI(adc_monitor_log_prefix, "Calibration scheme not supported, skip software calibration");
    }
    else if (ret == ESP_ERR_INVALID_VERSION)
    {
        ROB_LOGI(adc_monitor_log_prefix, "eFuse not burnt, skip software calibration");
    }
    else if (ret == ESP_OK)
    {
        cali_enabled = true;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    }
    else
    {
        ROB_LOGI(adc_monitor_log_prefix, "Invalid arg");
    }
    ROB_LOGI(adc_monitor_log_prefix, "Done Calibrating ADCs");
// TODO
// ADC1 config
#if !defined(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC) || CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC == 1
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(CONFIG_ROBUSTO_INPUT_ADC_MONITOR_ADC_CHANNEL, ADC_ATTEN_DB_11));

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