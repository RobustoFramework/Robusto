#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef uint16_t (*robusto_proxy_sdio_c6_topic_hook_t)(void *context,
													   const char *topic_name);

void robusto_proxy_sdio_c6_set_pubsub_topic_hooks(
	robusto_proxy_sdio_c6_topic_hook_t subscribe_hook,
	robusto_proxy_sdio_c6_topic_hook_t unsubscribe_hook,
	void *hook_context);

esp_err_t robusto_proxy_sdio_c6_start(void);
