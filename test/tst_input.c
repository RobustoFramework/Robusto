#include "tst_input.h"

// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>
#include <robusto_system.h>

bool pushed_5 = false;
bool pushed_2_6 = false;

resistance_mapping_t resistances[6] = {
    {.resistance = 194432, .adc_voltage = 2723, .adc_stdev = 3}, //This is the total resistance, or base voltage value
    {.resistance = 105527, .adc_voltage = 2255, .adc_stdev = 2},
    {.resistance = 48101, .adc_voltage = 2575, .adc_stdev = 4},
    {.resistance = 22195, .adc_voltage = 2663, .adc_stdev = 3},
    {.resistance = 10533, .adc_voltage = 2696, .adc_stdev = 2},
    {.resistance = 4801, .adc_voltage = 2711, .adc_stdev = 4},
};

resistor_ladder_t *ladder;

void callback_buttons_press(uint32_t buttons)
{
    // Button 2 and 4 are pressed simultaneously
    pushed_5 = buttons == 5;
    pushed_2_6 = buttons == 2 + 16;
};

void init_resistance_mappings()
{
    if (ladder) {
        return;
    }
    ladder = robusto_malloc(sizeof(resistor_ladder_t));
    ladder->mappings = &resistances;
    ladder->mapping_count = 6;
    ladder->callback = &callback_buttons_press;
    ladder->R1_resistance = 41200;
    ladder->voltage = 3300;
    ladder->GPIO = 32; // Usually OK.
    robusto_input_add_resistor_ladder(ladder);
}

/**
 * @brief Test a single value
 */
void tst_input_adc_single_resolve(void)
{
    ROB_LOGI("adc", "in tst_input_adc_single_resolve");
    init_resistance_mappings();
    robusto_input_test_resistor_ladder(2714, ladder);

    if (!robusto_waitfor_bool(&pushed_5, 1000))
    {
        TEST_ASSERT_TRUE_MESSAGE(pushed_5, "Didn't get the correct response");
    }
}

void tst_input_adc_multiple_resolve(void)
{
    #ifdef CONFIG_ROBUSTO_INPUT_ADC_MONITOR 
    #warning "The ADC monitor should be enabled for these tests to work, it may cause an ADC conflict"
    #endif
    ROB_LOGI("adc", "in tst_input_adc_multiple_resolve, button 2 and 5");
    init_resistance_mappings();
    uint32_t resistance = (resistances[2].resistance + resistances[5].resistance);
    double voltage = ((double)ladder->voltage * (double)resistance)/((double)ladder->R1_resistance + (double)resistance);
    ROB_LOGI("adc", "Calculated voltage: %f vs: %lu, from resistance %lu", voltage, ladder->voltage, resistance);
    robusto_input_test_resistor_ladder(voltage, ladder);
    if (!robusto_waitfor_bool(&pushed_2_6, 1000))
    {
        TEST_ASSERT_TRUE_MESSAGE(pushed_2_6, "Didn't get the correct response");
    }
    
}


