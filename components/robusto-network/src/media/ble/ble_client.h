
#ifndef _BLE_CLIENT_H_
#define _BLE_CLIENT_H_
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
    void ble_spp_client_on_reset(int reason);
    void ble_spp_client_on_sync(void);
    void ble_spp_client_host_task(void *param);
    /**********************
     *      MACROS
     **********************/


#endif
