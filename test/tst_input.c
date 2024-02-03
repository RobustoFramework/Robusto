#include "tst_input.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_input.h>

resistance_mapping_t bm;

void init_resistance_mappings()
{
    bm[CONFIG_ROBUSTO_INPUT_ADC_MONITOR_RESISTANCE_COUNT + 1] = {
        {.resistance = 189166, .adc_value = 3199, .adc_spread = 5},
        {.resistance = 86436, .adc_value = 2559, .adc_spread = 0},
        {.resistance = 142068, .adc_value = 2967, .adc_spread = 7},
        {.resistance = 184872, .adc_value = 3182, .adc_spread = 4},
        {.resistance = 178905, .adc_value = 3156, .adc_spread = 7},
        {.resistance = 167547, .adc_value = 3103, .adc_spread = 5},
    }
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void tst_fragmentation_complete(void)
{
    ROB_LOGI("fragmentation", "in tst_fragmentation_complete");
    skip_fragments = false;
    fake_message();
}

/**
 * @brief Fake a fragmented transmission, resending missing parts
 */
void tst_fragmentation_resending(void)
{
    ROB_LOGI("fragmentation", "in tst_fragmentation_resending");
    skip_fragments = true;
    fake_message();
}

#endif
