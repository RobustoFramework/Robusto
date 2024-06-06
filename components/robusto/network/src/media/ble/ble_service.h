/*
 * BLE service handling
 * Loosely based on the ESP-IDF-demo
 */
#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include <robusto_sys_queue.h>


/* 16 Bit SPP Service UUID */
#define GATT_SPP_SVC_UUID 0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define GATT_SPP_CHR_UUID 0xABF1



/** Misc. */
void print_bytes(const uint8_t *bytes, int len);
void print_addr(const void *addr);
int gatt_svr_register(void);
void ble_store_config_init(void);
uint16_t get_ble_spp_svc_gatt_read_val_handle();

void ble_init_service(char * _log_prefix);

#endif