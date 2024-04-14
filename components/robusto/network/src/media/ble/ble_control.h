

#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE

    /*********************
     *      DEFINES
     *********************/

    /*********************
     *      INCLUDES
     *********************/
    #include <stdbool.h>

    /**********************
     * GLOBAL PROTOTYPES
     **********************/
    
    void robusto_ble_init(char *log_prefix);
    void robusto_ble_shutdown();
    /**********************
     *      MACROS
     **********************/


#endif