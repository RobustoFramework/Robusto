#include "tst_input.h"
#ifdef CONFIG_ROBUSTO_INPUT

// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>
#include <robusto_system.h>

bool pushed_2_5 = false;

resistance_mapping_t resistances[6] = {
    {.resistance = 194432, .adc_voltage = 2723, .adc_stdev = 3}, //This is the total resistance, or base voltage value
    {.resistance = 105527, .adc_voltage = 2255, .adc_stdev = 2},
    {.resistance = 48101, .adc_voltage = 2575, .adc_stdev = 4},
    {.resistance = 22195, .adc_voltage = 2663, .adc_stdev = 3},
    {.resistance = 10533, .adc_voltage = 2696, .adc_stdev = 2},
    {.resistance = 4801, .adc_voltage = 2711, .adc_stdev = 4},
};

resistor_monitor_t *monitor = NULL;

void callback_buttons_press(uint32_t buttons, float voltage)
{
    // Button 2 and 4 are pressed simultaneously

    pushed_2_5 = buttons == 2 + 16;
    ROB_LOGI("adc", "test: Got a button event %lu with voltage %f", buttons, voltage);
};

void init_resistance_mappings()
{
    if (monitor) {
        return;
    }
    monitor = robusto_malloc(sizeof(resistor_monitor_t));
    monitor->mappings = resistances;
    monitor->mapping_count = 6;
    monitor->ladder_exclude_count = 0;
    monitor->callback = &callback_buttons_press;
    monitor->R1 = 41200;
    monitor->R2_check_resistor = 0;
    monitor->source_voltage = 3300;
    monitor->GPIO = 32; // Usually OK.
 //   robusto_input_add_resistor_monitor(monitor);    
}

/**
 * @brief Test a single value
 */
void tst_input_adc_single_resolve(void)
{
    ROB_LOGI("adc", "in tst_input_adc_single_resolve");
    init_resistance_mappings();
    
    TEST_ASSERT_TRUE_MESSAGE(match_single_resistor(2714, monitor->mappings[5]), "Didn't match the resistance");
    
}

void tst_input_adc_multiple_resolve(void)
{
    #ifdef CONFIG_ROBUSTO_INPUT_ADC_MONITOR 
    #warning "The ADC monitor should be enabled for these tests to work, it may cause an ADC conflict"
    #endif
    ROB_LOGI("adc", "in tst_input_adc_multiple_resolve, button 2 and 5");
    init_resistance_mappings();
    double resistance = resistances[0].resistance - (resistances[2].resistance + resistances[5].resistance);
    double voltage = ((double)monitor->source_voltage * (double)resistance)/((double)monitor->R1 + (double)resistance);
    ROB_LOGI("adc", "Calculated voltage: %1.f vs:  %1.f, from resistance %0.f", voltage, monitor->source_voltage, resistance);
    match_multiple_resistors(voltage, monitor);
    if (!robusto_waitfor_bool(&pushed_2_5, 1000))
    {
        TEST_ASSERT_TRUE_MESSAGE(pushed_2_5, "Didn't get the correct response");
    }
    
}

#endif