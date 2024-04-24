
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
    typedef enum ble_server_state {
        robusto_ble_stopped = 0x00,
        robusto_ble_starting = 0x01,
        robusto_ble_advertising = 0x02
    } ble_server_state_t;

    /**********************
     * GLOBAL PROTOTYPES
     **********************/
    void ble_spp_server_on_sync(void);
    void ble_spp_server_on_reset(int reason);
    void ble_server_init(char * _log_prefix);
    ble_server_state_t *ble_server_get_state_ptr(void);
    

    /**********************
     *      MACROS
     **********************/


#endif