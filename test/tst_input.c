#include "tst_input.h"

// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>
#include <robusto_system.h>

bool pushed_2_6;

resistance_mapping_t resistances[6] = {
    {.resistance = 194841, .adc_voltage = 2724, .adc_spread = 3}, // This is the total resistance, or base voltage value
    {.resistance = 88905, .adc_voltage = 2255, .adc_spread = 2},
    {.resistance = 147110, .adc_voltage = 2578, .adc_spread = 5},
    {.resistance = 172910, .adc_voltage = 2665, .adc_spread = 4},
    {.resistance = 183899, .adc_voltage = 2696, .adc_spread = 2},
    {.resistance = 190418, .adc_voltage = 2713, .adc_spread = 3},
};

resistor_ladder_t *ladder;

void callback_buttons_press(uint32_t buttons)
{
    // Button 2 and 4 are pressed simultaneously
    pushed_2_6 = buttons == 6;
};

void init_resistance_mappings()
{

    ladder = robusto_malloc(sizeof(resistor_ladder_t));
    ladder->mappings = &resistances;
    ladder->mapping_count = 6;
    ladder->callback = &callback_buttons_press;
    ladder->R1_resistance = 41200;
    ladder->voltage = 3.3;
    robusto_input_add_resistor_ladder(ladder);
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void tst_input_adc_resolve(void)
{
    ROB_LOGI("adc", "in tst_input_adc_resolve");
    init_resistance_mappings();
    robusto_input_test_resistor_ladder(2713, ladder);

    if (!robusto_waitfor_bool(&pushed_2_6, 1000))
    {
        TEST_ASSERT_TRUE_MESSAGE(pushed_2_6, "Didn't get the correct response");
    }
}
