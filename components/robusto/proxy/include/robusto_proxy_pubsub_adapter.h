#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT 4U

typedef uint16_t (*robusto_proxy_pubsub_local_callback_t)(
    void *context, const uint8_t *data, uint32_t data_length);

typedef struct robusto_proxy_pubsub_backend {
    uint16_t (*publish)(void *context, const char *topic,
                        const uint8_t *data, uint32_t data_length,
                        uint32_t *topic_hash, uint32_t *delivery_count);
    uint16_t (*subscribe)(void *context, const char *topic,
                          robusto_proxy_pubsub_local_callback_t callback,
                          void *callback_context, uint32_t *topic_hash);
    uint16_t (*unsubscribe)(void *context, uint32_t topic_hash,
                            robusto_proxy_pubsub_local_callback_t callback,
                            void *callback_context);
} robusto_proxy_pubsub_backend_t;

typedef struct robusto_proxy_pubsub_lock {
    bool (*take)(void *context);
    void (*give)(void *context);
    void *context;
} robusto_proxy_pubsub_lock_t;

struct robusto_proxy_pubsub_server_adapter;

typedef struct robusto_proxy_pubsub_subscription {
    struct robusto_proxy_pubsub_server_adapter *adapter;
    robusto_proxy_pubsub_local_callback_t delivery_callback;
    void *delivery_callback_context;
    uint32_t subscription_id;
    uint32_t topic_hash;
    uint32_t next_delivery_sequence;
    uint16_t topic_length;
    bool active;
    char topic[ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U];
} robusto_proxy_pubsub_subscription_t;

typedef struct robusto_proxy_pubsub_event_descriptor {
    uint32_t subscription_id;
    uint32_t delivery_sequence;
    uint32_t data_length;
    uint32_t pool_offset;
} robusto_proxy_pubsub_event_descriptor_t;

typedef struct robusto_proxy_pubsub_server_adapter {
    const robusto_proxy_pubsub_backend_t *backend;
    void *backend_context;
    robusto_proxy_pubsub_lock_t lock;
    robusto_proxy_pubsub_subscription_t *subscriptions;
    uint16_t subscription_capacity;
    uint16_t active_subscriptions;
    uint32_t next_subscription_id;
    robusto_proxy_pubsub_event_descriptor_t events[ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT];
    uint8_t *event_pool;
    uint32_t event_pool_capacity;
    uint32_t event_pool_read;
    uint32_t event_pool_write;
    uint32_t event_pool_used;
    uint8_t event_read_index;
    uint8_t event_write_index;
    uint8_t event_count;
    uint32_t publish_requests;
    uint32_t subscribe_requests;
    uint32_t unsubscribe_requests;
    uint32_t delivery_events;
    uint32_t delivery_drops;
    uint32_t duplicate_operations;
    uint32_t pubsub_errors;
} robusto_proxy_pubsub_server_adapter_t;

bool robusto_proxy_pubsub_server_adapter_init(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    const robusto_proxy_pubsub_backend_t *backend,
    void *backend_context,
    robusto_proxy_pubsub_subscription_t *subscriptions,
    uint16_t subscription_capacity,
    uint8_t *event_pool,
    uint32_t event_pool_capacity,
    robusto_proxy_pubsub_lock_t lock);

bool robusto_proxy_pubsub_server_adapter_deinit(
    robusto_proxy_pubsub_server_adapter_t *adapter);

const robusto_proxy_pubsub_adapter_t *robusto_proxy_pubsub_server_adapter_operations(void);

bool robusto_proxy_pubsub_server_adapter_take_delivery(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    uint8_t *payload_buffer,
    size_t payload_buffer_size,
    size_t *payload_size);

#ifdef __cplusplus
}
#endif