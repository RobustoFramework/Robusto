#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "robusto_proxy_pubsub_adapter.h"

typedef uint16_t (*robusto_c6_pubsub_topic_hook_t)(void *context,
                                                   const char *topic_name);

typedef struct robusto_c6_pubsub_backend {
    StaticSemaphore_t mutex_storage;
    SemaphoreHandle_t mutex;
} robusto_c6_pubsub_backend_t;

void robusto_c6_pubsub_backend_set_topic_hooks(
    robusto_c6_pubsub_topic_hook_t subscribe_hook,
    robusto_c6_pubsub_topic_hook_t unsubscribe_hook,
    void *hook_context);

bool robusto_c6_pubsub_backend_init(
    robusto_c6_pubsub_backend_t *backend,
    robusto_proxy_pubsub_server_adapter_t *adapter,
    robusto_proxy_pubsub_subscription_t *subscriptions,
    uint16_t subscription_capacity,
    uint8_t *event_pool,
    uint32_t event_pool_capacity);

void robusto_c6_pubsub_backend_deinit(robusto_c6_pubsub_backend_t *backend);