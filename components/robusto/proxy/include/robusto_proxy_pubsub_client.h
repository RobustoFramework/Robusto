#pragma once

#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_client.h"
#include "robusto_proxy_pubsub.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_PUBSUB_CLIENT_SUBSCRIPTION_STORAGE_BYTES 304U

typedef union robusto_proxy_pubsub_client_subscription {
    max_align_t alignment;
    uint8_t private_storage[ROBUSTO_PROXY_PUBSUB_CLIENT_SUBSCRIPTION_STORAGE_BYTES];
} robusto_proxy_pubsub_client_subscription_t;

typedef rob_ret_val_t (robusto_proxy_pubsub_callback)(
    void *context,
    uint8_t *data,
    uint32_t data_length);

bool robusto_proxy_pubsub_is_ready(const robusto_proxy_client_t *client);

rob_ret_val_t robusto_proxy_pubsub_configure(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_client_subscription_t *subscriptions,
    uint16_t subscription_capacity);

rob_ret_val_t robusto_proxy_pubsub_subscribe(
    robusto_proxy_client_t *client,
    const char *topic_name,
    robusto_proxy_pubsub_callback *callback,
    void *callback_context,
    robusto_proxy_pubsub_client_subscription_t **subscription);

rob_ret_val_t robusto_proxy_pubsub_unsubscribe(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_client_subscription_t *subscription);

rob_ret_val_t robusto_proxy_pubsub_publish(
    robusto_proxy_client_t *client,
    const char *topic_name,
    uint8_t *data,
    uint32_t data_length);

rob_ret_val_t robusto_proxy_pubsub_query_status(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_status_response_t *response);

rob_ret_val_t robusto_proxy_pubsub_handle_event(
    robusto_proxy_client_t *client,
    const uint8_t *event_frame,
    size_t event_frame_size);

void robusto_proxy_pubsub_session_reset(robusto_proxy_client_t *client);

rob_ret_val_t robusto_proxy_pubsub_reconcile(robusto_proxy_client_t *client);

#ifdef __cplusplus
}
#endif