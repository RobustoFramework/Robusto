
#pragma once

#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
/*********************
 *      INCLUDES
 *********************/

#include "ble_spp.h"

/*********************
 *      DEFINES
 *********************/


void ble_on_disc_complete(const ble_peer *peer, int status, void *arg);
void ble_on_reset(int reason);
int ble_negotiate_mtu(uint16_t conn_handle);
void ble_host_task(void *param);
void report_ble_connection_error(int conn_handle, int code);
int ble_send_message(uint16_t conn_handle, void *data, int data_length);

void init_ble_global(char * _log_prefix);
#endif
