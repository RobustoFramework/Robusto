
/**
 * @file spi_hal_esp32.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto SPI-HAL for esp32
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "spi_hal.h"

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

#include <stdint.h>
#include <string.h>


static uint8_t g_rx_buffer[20];

// Define SPI transaction configuration
spi_transaction_t spi_transaction_config = {
  .flags = 0,
  .cmd = 0,
  .addr = 0,
  .length = 0,
  .rxlength = 0,
  .user = NULL,
  .tx_buffer = NULL,
  .rx_buffer = NULL
};

spi_device_handle_t robusto_spiBegin(spi_host_device_t host, const spi_device_interface_config_t *devcfg) {
    spi_device_handle_t spi;
    esp_err_t ret;
    ret = spi_bus_add_device(host, devcfg, &spi);
    assert(ret == ESP_OK);
    return spi;  
     
}

void robusto_spiBeginTransaction(spi_device_handle_t spi) {
    // Nothing to do here in ESP-IDF (the transaction concept doesn't seem to be part of the SPI protocol)
}



void robusto_spiEndTransaction(spi_device_handle_t spi) {
    // Nothing to do here in ESP-IDF
}


uint8_t robusto_spiTransfer_byte(spi_device_handle_t spi, uint8_t data) {
    esp_err_t ret;
    spi_transaction_config.length = 8;
    spi_transaction_config.tx_data[0] = data;
    spi_transaction_config.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    ret = spi_device_transmit(spi, &spi_transaction_config);
    assert(ret == ESP_OK);
    return spi_transaction_config.rx_data[0];
}

void robusto_spiTransfer_data(spi_device_handle_t spi, const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = tx_data;
    t.rx_buffer = rx_data;
    esp_err_t ret;
    ret = spi_device_transmit(spi, &t);
    assert(ret == ESP_OK);
}
void robusto_spiEnd(spi_device_handle_t spi) {
    esp_err_t ret;
    ret = spi_bus_remove_device(spi);
    assert(ret == ESP_OK);
}
#endif