
#ifndef _BLE_GLOBAL_H_
#define _BLE_GLOBAL_H_
/*********************
 *      INCLUDES
 *********************/

#include "ble_spp.h"

/*********************
 *      DEFINES
 *********************/
SemaphoreHandle_t BLE_Semaphore;

void ble_on_disc_complete(const ble_peer *peer, int status, void *arg);
void ble_on_reset(int reason);
int ble_negotiate_mtu(uint16_t conn_handle);
void ble_host_task(void *param);
void report_ble_connection_error(int conn_handle, int code);
int ble_send_message(uint16_t conn_handle, void *data, int data_length);

#endif
