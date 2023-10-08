/*
 * BLE service handling
 * Loosely based on the ESP-IDF-demo
 */

#ifndef _BLE_SERVICE_H_
#define _BLE_SERVICE_H_
#include <os/queue.h>

/* This semaphore is use for blocking, so different threads doesn't accidentaly communicate at the same time */
SemaphoreHandle_t xBLE_Comm_Semaphore;

/* 16 Bit SPP Service UUID */
#define GATT_SPP_SVC_UUID 0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define GATT_SPP_CHR_UUID 0xABF1

uint16_t ble_svc_gatt_read_val_handle, ble_spp_svc_gatt_read_val_handle;

/** Misc. */
void print_bytes(const uint8_t *bytes, int len);
void print_addr(const void *addr);
int gatt_svr_register(void);
void ble_store_config_init(void);

void ble_init_service(char * _log_prefix);

#endif
