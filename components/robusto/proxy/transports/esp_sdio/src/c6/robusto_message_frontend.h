#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*robusto_message_receive_fn)(uint32_t message_id,
                                           const uint8_t *data,
                                           size_t data_len);

esp_err_t robusto_message_frontend_register(uint32_t message_id,
                                            robusto_message_receive_fn receive);
esp_err_t robusto_message_frontend_send(uint32_t message_id,
                                        const uint8_t *data,
                                        size_t data_len);