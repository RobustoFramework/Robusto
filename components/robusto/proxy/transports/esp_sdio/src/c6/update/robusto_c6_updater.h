#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

typedef esp_err_t (*robusto_c6_updater_receive_fn)(const uint8_t *data,
												   size_t data_len);
typedef esp_err_t (*robusto_c6_updater_start_receive_fn)(
	robusto_c6_updater_receive_fn receive);
typedef esp_err_t (*robusto_c6_updater_send_fn)(const uint8_t *data,
												size_t data_len);

typedef struct {
	robusto_c6_updater_start_receive_fn start_receive;
	robusto_c6_updater_send_fn send;
} robusto_c6_updater_frontend_t;

esp_err_t robusto_c6_updater_init(
	const robusto_c6_updater_frontend_t *frontend);
esp_err_t robusto_c6_updater_submit(const uint8_t *data, size_t data_len);