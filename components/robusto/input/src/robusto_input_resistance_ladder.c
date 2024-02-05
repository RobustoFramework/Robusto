
#include <robusto_input.h>

SLIST_HEAD(resistor_ladders, resistor_ladder);


/**
 * @brief Add a monitor for a GPIO
 * 
 */

rob_ret_val_t robusto_input_add_resistor_ladder(resistor_ladder_t * map) {
    return ROB_OK;
};

rob_ret_val_t robusto_input_test_resistor_ladder(uint32_t adc_val, resistor_ladder_t * map){
    return ROB_OK;
};