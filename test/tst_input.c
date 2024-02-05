#include "tst_input.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>

bool pushed_2_6;

resistance_mapping_t bm[6] = {
    {.resistance = 189565, .adc_value = 3201, .adc_spread = 6}, //This is the total resistance, or base voltage value
    {.resistance = 86436, .adc_value = 2559, .adc_spread = 0},
    {.resistance = 142825, .adc_value = 2971, .adc_spread = 5},
    {.resistance = 168860, .adc_value = 3109, .adc_spread = 6},
    {.resistance = 180000, .adc_value = 3160, .adc_spread = 5},
    {.resistance = 185641, .adc_value = 3184, .adc_spread = 3},
}

void cb_buttons_press(uint32_t buttons)
{
    // Button 2 and 4 are pressed simultaneously
    pushed_2_6 = buttons == 6;

};

void init_resistance_mappings()
{

}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void tst_fragmentation_complete(void)
{
    ROB_LOGI("fragmentation", "in tst_fragmentation_complete");
    robusto_input_analyze_adc(uint8_t)
    if (!robusto_waitfor_bool(&pushed_2_6, 1000)) {
        TEST_ASSERT_BOO
    }
    
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
