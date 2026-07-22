#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t robusto_proxy_sdio_host_init(void);
esp_err_t robusto_proxy_sdio_host_send(uint32_t message_id,
                                    const uint8_t *payload,
                                    size_t payload_size,
                                    uint32_t timeout_ms);
esp_err_t robusto_proxy_sdio_host_receive(uint32_t *message_id,
                                       uint8_t *payload,
                                       size_t payload_capacity,
                                       size_t *payload_size,
                                       uint32_t timeout_ms);
esp_err_t robusto_proxy_sdio_host_deinit(void);