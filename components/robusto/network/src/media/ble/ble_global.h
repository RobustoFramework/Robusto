
#pragma once

#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
/*********************
 *      INCLUDES
 *********************/

#include "ble_spp.h"
#include <robusto_peer.h>

/*********************
 *      DEFINES
 *********************/


void ble_on_disc_complete(const ble_peer *peer, int status, void *arg);
void ble_on_reset(int reason);
int ble_negotiate_mtu(uint16_t conn_handle);
void ble_host_task(void *param);
void report_ble_connection_error(int conn_handle, int code);
rob_ret_val_t ble_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);

void init_ble_global(char * _log_prefix);
#endif
