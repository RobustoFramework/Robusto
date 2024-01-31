#include "robusto_input_init.h"
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include <inttypes.h>

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
    skip_count : 10,
    skips_left : 0,
    recurrence_callback : &monitor_adc_cb,
    shutdown_callback : &monitor_adc_shutdown_cb
};

#ifdef USE_ESPIDF


//ADC Calibration
#if CONFIG_IDF_TARGET_ESP32
#define ADC_MONITOR_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_MONITOR_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_MONITOR_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_MONITOR_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif

#endif

#if defined(ADC1_CHANNEL_4)

static int adc_raw[2][10];

static esp_adc_cal_characteristics_t adc1_chars;
static esp_adc_cal_characteristics_t adc2_chars;

bool cali_enabled = false;

void monitor_adc_cb() {
    uint32_t voltage = 0;
    int adc_raw = adc1_get_raw(ADC1_CHANNEL_4);
    if (cali_enabled) {
        voltage = esp_adc_cal_raw_to_voltage(adc_raw, &adc1_chars);
        ROB_LOGI(adc_monitor_log_prefix, "cali data: %" PRIu32 " mV, raw: %i", voltage, adc_raw);
    }

};
void monitor_adc_shutdown_cb() {
    ROB_LOGI(adc_monitor_log_prefix, "Stop!!");
};

/**
 * @brief Start monitoring
 *
 */
void robusto_input_start_adc_monitoring() {
    
    ROB_LOGI(adc_monitor_log_prefix, "Starting ADC logging on GPIO %i", CONFIG_ROBUSTO_INPUT_ADC_MONITOR_GPIO);
    robusto_register_recurrence(&memory_monitor);

}

#ifdef USE_ESPIDF

void adc_calibration_init(void)
{
    esp_err_t ret;
    cali_enabled = false;
    ROB_LOGI(adc_monitor_log_prefix, "Calibrating ADCs");
    ret = esp_adc_cal_check_efuse(ADC_MONITOR_CALI_SCHEME);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ROB_LOGI(adc_monitor_log_prefix, "Calibration scheme not supported, skip software calibration");
    } else if (ret == ESP_ERR_INVALID_VERSION) {
        ROB_LOGI(adc_monitor_log_prefix, "eFuse not burnt, skip software calibration");
    } else if (ret == ESP_OK) {
        cali_enabled = true;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
        esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc2_chars);
    } else {
        ROB_LOGI(adc_monitor_log_prefix, "Invalid arg");
    }
    ROB_LOGI(adc_monitor_log_prefix, "Done Calibrating ADCs");
    // TODO
    //ADC1 config
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11));
}

#endif
void robusto_input_init_adc_monitoring(char * _log_prefix) {
    adc_monitor_log_prefix = _log_prefix;
#ifdef USE_ESPIDF
    adc_calibration_init();
#endif
}