#pragma once

#include "esp_err.h"
#include "robusto_proxy_pubsub.h"
#include "robusto_proxy_protocol.h"
#include "robusto_rsd1_protocol.h"

#define ROBUSTO_PROXY_SDIO_SLAVE_MAX_PACKET_SIZE 4092U
#define ROBUSTO_PROXY_SDIO_SLAVE_MAX_FRONTEND_PAYLOAD_SIZE \
	(ROBUSTO_PROXY_SDIO_SLAVE_MAX_PACKET_SIZE - ROBUSTO_RSD1_HEADER_SIZE - \
	 ROBUSTO_RSD1_CRC_SIZE)
#define ROBUSTO_PROXY_SDIO_SLAVE_MAX_EVENT_PAYLOAD_SIZE \
	(ROBUSTO_PROXY_SDIO_SLAVE_MAX_FRONTEND_PAYLOAD_SIZE - \
	 ROBUSTO_PROXY_HEADER_SIZE_BYTES - ROBUSTO_PROXY_CRC_SIZE_BYTES)
#define ROBUSTO_PROXY_SDIO_SLAVE_MAX_DELIVERY_CHUNK_DATA_SIZE \
	(ROBUSTO_PROXY_SDIO_SLAVE_MAX_EVENT_PAYLOAD_SIZE - \
	 ROBUSTO_PROXY_PUBSUB_DELIVERY_CHUNK_HEADER_SIZE_BYTES)

_Static_assert(ROBUSTO_PROXY_SDIO_SLAVE_MAX_FRONTEND_PAYLOAD_SIZE == 4068U,
			   "C6 SDIO frontend payload budget changed");
_Static_assert(ROBUSTO_PROXY_SDIO_SLAVE_MAX_EVENT_PAYLOAD_SIZE == 4044U,
			   "C6 SDIO proxy event payload budget changed");
_Static_assert(ROBUSTO_PROXY_SDIO_SLAVE_MAX_DELIVERY_CHUNK_DATA_SIZE == 4028U,
			   "C6 SDIO delivery chunk budget changed");

esp_err_t robusto_proxy_sdio_device_init(void);
esp_err_t robusto_proxy_sdio_device_start(void);