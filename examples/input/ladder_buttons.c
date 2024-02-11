#include "ladder_buttons.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>
#include <robusto_system.h>

resistance_mapping_t resistances[6] = {
    {.resistance = 194432, .adc_voltage = 2723, .adc_stdev = 4}, // This is the total resistance, or base voltage value
    {.resistance = 105464, .adc_voltage = 2255, .adc_stdev = 2},
    {.resistance = 47712, .adc_voltage = 2575, .adc_stdev = 4},
    {.resistance = 22026, .adc_voltage = 2663, .adc_stdev = 3},
    {.resistance = 10532, .adc_voltage = 2696, .adc_stdev = 3},
    {.resistance = 4995, .adc_voltage = 2710, .adc_stdev = 4},
};

resistor_monitor_t *monitor;
uint32_t change_count = 0;

void callback_buttons_press(uint32_t buttons)
{

    char bitString[33]; // 32 bits + null terminator
    for (int i = 0; i < 33; i++)
    {
        bitString[i] = (buttons & (1 << i)) ? '0' + i : ' ';
    }
    bitString[32] = '\0'; // Null-terminate the string

    ROB_LOGI("LADDER BUTTONS", "Change count: %lu , Value: %lu Buttons pressed: %s", change_count++, buttons, bitString);
};

void init_resistance_mappings()
{
    if (monitor)
    {
        return;
    }
    monitor = robusto_malloc(sizeof(resistor_monitor_t));
    monitor->mappings = &resistances;
    monitor->mapping_count = 6;
    monitor->callback = &callback_buttons_press;
    monitor->R1 = 41200;
    monitor->R2_check_resistor = 2200;
    monitor->source_voltage = 3300;
    monitor->GPIO = 32; // Usually OK.
    monitor->name = "Ladder monitor";
    monitor->ladder_decode = true;
    monitor->ladder_exclude_count = 0;

    if (robusto_input_add_resistor_monitor(monitor) != ROB_OK)
    {
        ROB_LOGE("LADDER BUTTONS", "Failed to initialize!");
    }
}

void ladder_buttons_init(void)
{
    ROB_LOGI("LADDER BUTTONS", "In ladder_buttons_init");
    init_resistance_mappings();
}
