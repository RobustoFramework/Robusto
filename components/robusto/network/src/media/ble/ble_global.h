
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
/**
 * @brief Sends a message without assessment of length and so forth, internal use
 * 
 * @param peer 
 * @param data 
 * @param data_length 
 * @param receipt 
 * @return rob_ret_val_t 
 */
rob_ret_val_t ble_send_message_raw(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);
rob_ret_val_t ble_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);
/**
 * @brief BLE addresses are presented in reverse and has an offset (+2) against the base MAC
 * 
 * @param mac_address source data
 * @param dest_address destination structure
 */
void ble_to_base_mac_address(uint8_t *mac_address, uint8_t *dest_address);

/**
 * @brief Make a base MAC address into a BLE address, which is reversed and has an offset (+2) against the base MAC
 * 
 * @param mac_address source data
 * @param dest_address destination structure
 */

void base_mac_to_ble_address(uint8_t *mac_address, uint8_t *dest_address);

void ble_global_init(char * _log_prefix);
#endif
