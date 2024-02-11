#include "ladder_buttons.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <robusto_logging.h>
#include <robusto_input.h>
#include <robusto_concurrency.h>
#include <robusto_system.h>

bool pushed_5 = false;
bool pushed_2_6 = false;

resistance_mapping_t resistances[6] = {
    {.resistance = 194432, .adc_voltage = 2723, .adc_stdev = 4}, //This is the total resistance, or base voltage value
    {.resistance = 105527, .adc_voltage = 2255, .adc_stdev = 2},
    {.resistance = 48101, .adc_voltage = 2575, .adc_stdev = 4},
    {.resistance = 22195, .adc_voltage = 2663, .adc_stdev = 3},
    {.resistance = 10533, .adc_voltage = 2696, .adc_stdev = 2},
    {.resistance = 4801, .adc_voltage = 2711, .adc_stdev = 4},
};

resistor_ladder_t *ladder;

void callback_buttons_press(uint32_t buttons)
{

      char bitString[33]; // 8 bits + null terminator
    for (int i = 0; i < 33; i++) {
        bitString[i] = (buttons & (1 << i)) ? '0' + i : ' ';
    }
    bitString[32] = '\0'; // Null-terminate the string


 
    ROB_LOGI("LADDER BUTTONS","Value: %lu Buttons pressed: %s", buttons, bitString);

    /*
    uint8_t b = 1;
    for (uint8_t i = 1; i < 129; i = i * 2) {
        if (buttons & i) {
            ROB_LOGI("LADDER BUTTONS","Button %hu is pressed", b);
        }
        b++;
    }
    */

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
    if (robusto_input_add_resistor_ladder(ladder) != ROB_OK) {
        ROB_LOGE("LADDER BUTTONS", "Failed to initialize!");
    }
}

void ladder_buttons_init(void)
{
    ROB_LOGI("LADDER BUTTONS", "In ladder_buttons_init");
    init_resistance_mappings();
   
}
 


