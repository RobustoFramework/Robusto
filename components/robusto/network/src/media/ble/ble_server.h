
#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
/*********************
 *      DEFINES
 *********************/


/*********************
 *      INCLUDES
 *********************/



    /**********************
     *      TYPEDEFS
     **********************/

    /**********************
     * GLOBAL PROTOTYPES
     **********************/
    void ble_spp_server_on_sync(void);
    void ble_spp_server_on_reset(int reason);

    /**********************
     *      MACROS
     **********************/


#endif