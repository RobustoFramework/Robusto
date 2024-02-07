
#include <robusto_input.h>
#include <robusto_logging.h>

SLIST_HEAD(resistor_ladder_head, resistor_ladder);

struct resistor_ladder_head ladder_head;

static char *input_log_prefix;

/**
 * @brief Add a monitor for a GPIO
 *
 */

rob_ret_val_t robusto_input_add_resistor_ladder(resistor_ladder_t *map)
{
    // TODO: Add checks on structure
    SLIST_INSERT_HEAD(&ladder_head, map, resistor_ladders);
    return ROB_OK;
};

bool match_single_resistor(uint32_t adc_voltage, resistance_mapping_t mapping)
{

    ROB_LOGD(input_log_prefix, "Comparing %lu to %u +/- %u", adc_voltage, mapping.adc_voltage, mapping.adc_stdev);
    if ((adc_voltage <= (mapping.adc_voltage + mapping.adc_stdev)) &&
        (adc_voltage >= (mapping.adc_voltage - mapping.adc_stdev)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

rob_ret_val_t robusto_input_test_resistor_ladder(double adc_voltage, resistor_ladder_t *ladder)
{
    if (!ladder)
    {
        return ROB_FAIL;
    }
    ROB_LOGI(input_log_prefix, "Looping %hu resistances", ladder->mapping_count);

    for (uint8_t curr_map = 1; curr_map < ladder->mapping_count; curr_map++)
    {

        if (match_single_resistor(adc_voltage, ladder->mappings[curr_map]))
        {
            ROB_LOGI(input_log_prefix, "Matched number %hu on %.1f to %u.", curr_map, adc_voltage, ladder->mappings[curr_map].adc_voltage);
            ladder->callback(curr_map);
            return ROB_OK;
            
        }
    }
    double resistance = (-(double)adc_voltage * (double)ladder->R1_resistance)/((double)adc_voltage - (double)ladder->voltage);
    ROB_LOGD(input_log_prefix, "Not single press, calculated resistance : %.1f.", resistance);
    uint32_t curr_resistance;
    uint8_t matches = 0;

    for (uint8_t curr_map = 1; curr_map < ladder->mapping_count; curr_map++)
    {
        curr_resistance = ladder->mappings[curr_map].resistance;
        ROB_LOGD(input_log_prefix, "Resistance left %.1f curr res %lu curr stdev %u", resistance, curr_resistance, ladder->mappings[curr_map].adc_stdev); 
        if (resistance >= (curr_resistance - ladder->mappings[curr_map].adc_stdev)) {
            matches |= 1 << (curr_map - 1); 
            resistance-= curr_resistance; 
            ROB_LOGI(input_log_prefix, "Resistance included and subtracted %lu", curr_resistance);
        }
    }
    if (matches > 0) {
        ROB_LOGI(input_log_prefix, "Multiple buttons pressed, %hu", matches);
        ladder->callback(matches);
    }
    
    // TODO: Add the check on nothing pressed and if needed and the voltage is within resistance[0] range,
    // adjust ladder->voltage up with the appropriate amount
    return ROB_OK;
};

/**
 * @brief Start resistance
 *
 */
void robusto_input_resistance_ladder_init(char *_input_log_prefix)
{
    input_log_prefix = _input_log_prefix;
    ROB_LOGI(input_log_prefix, "Input resistance ladder initiated.");
}
