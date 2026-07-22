#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "robusto_proxy_pubsub_client.h"

rob_ret_val_t robusto_proxy_sdio_register(
    robusto_proxy_pubsub_client_subscription_t *subscriptions,
    uint16_t subscription_capacity);

robusto_proxy_client_t *robusto_proxy_sdio(void);

bool robusto_proxy_sdio_is_registered(void);
