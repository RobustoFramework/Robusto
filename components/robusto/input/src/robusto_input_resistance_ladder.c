
#include <robusto_input.h>
#include <robusto_logging.h>

SLIST_HEAD(resistor_ladder_head, resistor_ladder);

struct resistor_ladder_head ladder_head;

static char * input_log_prefix; 

/**
 * @brief Add a monitor for a GPIO
 * 
 */

rob_ret_val_t robusto_input_add_resistor_ladder(resistor_ladder_t * map) {
    // TODO: Add checks on structure
    SLIST_INSERT_HEAD(&ladder_head, map, resistor_ladders);
    return ROB_OK;
};

bool match_resistor(uint32_t adc_voltage, resistance_mapping_t mapping) {

    ROB_LOGI(input_log_prefix, "Comparing %lu to %u +/- %u", adc_voltage, mapping.adc_voltage, mapping.adc_spread);
    if ((adc_voltage < (mapping.adc_voltage + mapping.adc_spread)) ||
    (adc_voltage > (mapping.adc_voltage - mapping.adc_spread))) {
        return true;
    } else {
        return false;
    }
}

rob_ret_val_t robusto_input_test_resistor_ladder(uint32_t adc_voltage, resistor_ladder_t * ladder){
    if (!ladder) {
        return ROB_FAIL;
    }
    ROB_LOGI(input_log_prefix, "Looping %hu resistances", ladder->mapping_count);
    // TODO: Should the last be first instead?
    for (uint8_t curr_map= 0; curr_map < ladder->mapping_count; curr_map++) {
        if (match_resistor(adc_voltage, ladder->mappings[curr_map])) {
            ROB_LOGI(input_log_prefix, "Match on %u to %lu", ladder->mappings[curr_map].adc_voltage, adc_voltage);
            ladder->callback(curr_map);
            break;
        }
    }

    
    return ROB_OK;
};


/**
 * @brief Start resistance
 *
 */
void robusto_input_resistance_ladder_init(char * _input_log_prefix)
{
    input_log_prefix = _input_log_prefix;
    ROB_LOGI(input_log_prefix, "Input resistance ladder initiated.");

}
