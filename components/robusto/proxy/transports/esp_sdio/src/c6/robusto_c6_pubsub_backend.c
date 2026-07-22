#include "robusto_c6_pubsub_backend.h"

#include "esp_log.h"
#include "robusto_pubsub_server.h"
#include "robusto_retval.h"

static const char *TAG = "c6_pubsub";

static uint16_t map_robusto_publish_result(rob_ret_val_t result)
{
    switch (result)
    {
        case ROB_OK:
            return ROBUSTO_PROXY_STATUS_OK;
        case ROB_ERR_OUT_OF_MEMORY:
            return ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY;
        case ROB_ERR_INVALID_ARG:
            return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
        case ROB_ERR_MUTEX:
        case ROB_ERR_QUEUE_FULL:
            return ROBUSTO_PROXY_STATUS_BUSY;
        case ROB_ERR_SEND_FAIL:
        case ROB_ERR_SEND_SOME_FAIL:
            return ROBUSTO_PROXY_STATUS_PUBSUB_DELIVERY_FAILED;
        default:
            return ROBUSTO_PROXY_STATUS_INTERNAL;
    }
}

static rob_ret_val_t robusto_delivery_callback(void *context,
                                                uint8_t *data,
                                                uint32_t data_length)
{
    robusto_proxy_pubsub_subscription_t *subscription = context;
    uint16_t status;

    if (subscription == NULL || subscription->delivery_callback == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    status = subscription->delivery_callback(subscription->delivery_callback_context,
                                              data, data_length);
    switch (status)
    {
        case ROBUSTO_PROXY_STATUS_OK:
            return ROB_OK;
        case ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY:
            return ROB_ERR_OUT_OF_MEMORY;
        case ROBUSTO_PROXY_STATUS_BUSY:
            return ROB_ERR_MUTEX;
        default:
            return ROB_FAIL;
    }
}

static uint16_t backend_publish(void *context,
                                const char *topic,
                                const uint8_t *data,
                                uint32_t data_length,
                                uint32_t *topic_hash,
                                uint32_t *delivery_count)
{
    pubsub_server_topic_t *robusto_topic;
    rob_ret_val_t result;

    (void)context;
    if (topic == NULL || (data_length > 0U && data == NULL) ||
        topic_hash == NULL || delivery_count == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    robusto_topic = robusto_pubsub_server_find_or_create_topic((char *)topic);
    if (robusto_topic == NULL)
    {
        return ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY;
    }
    *topic_hash = robusto_topic->hash;
    *delivery_count = robusto_topic->subscriber_count;
    result = robusto_pubsub_server_publish(robusto_topic->hash,
                                           (uint8_t *)data, data_length);
    return map_robusto_publish_result(result);
}

static uint16_t backend_subscribe(void *context,
                                  const char *topic,
                                  robusto_proxy_pubsub_local_callback_t callback,
                                  void *callback_context,
                                  uint32_t *topic_hash)
{
    robusto_proxy_pubsub_subscription_t *subscription = callback_context;
    uint32_t hash;

    (void)context;
    if (topic == NULL || callback == NULL || subscription == NULL ||
        topic_hash == NULL || subscription->delivery_callback != callback ||
        subscription->delivery_callback_context != callback_context)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    hash = robusto_pubsub_server_subscribe_with_context(
        robusto_delivery_callback, subscription, (char *)topic);
    if (hash == 0U)
    {
        return ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY;
    }
    *topic_hash = hash;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t backend_unsubscribe(void *context,
                                    uint32_t topic_hash,
                                    robusto_proxy_pubsub_local_callback_t callback,
                                    void *callback_context)
{
    robusto_proxy_pubsub_subscription_t *subscription = callback_context;
    uint32_t removed_hash;

    (void)context;
    if (topic_hash == 0U || callback == NULL || subscription == NULL ||
        subscription->delivery_callback != callback ||
        subscription->delivery_callback_context != callback_context)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    removed_hash = robusto_pubsub_server_unsubscribe_with_context(
        robusto_delivery_callback, subscription, topic_hash);
    return removed_hash == topic_hash ? ROBUSTO_PROXY_STATUS_OK
                                      : ROBUSTO_PROXY_STATUS_INTERNAL;
}

static bool backend_lock_take(void *context)
{
    robusto_c6_pubsub_backend_t *backend = context;
    return backend != NULL && backend->mutex != NULL &&
           xSemaphoreTake(backend->mutex, portMAX_DELAY) == pdTRUE;
}

static void backend_lock_give(void *context)
{
    robusto_c6_pubsub_backend_t *backend = context;
    if (backend != NULL && backend->mutex != NULL)
    {
        if (xSemaphoreGive(backend->mutex) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to release PubSub adapter mutex");
        }
    }
}

static const robusto_proxy_pubsub_backend_t backend_operations = {
    .publish = backend_publish,
    .subscribe = backend_subscribe,
    .unsubscribe = backend_unsubscribe,
};

bool robusto_c6_pubsub_backend_init(
    robusto_c6_pubsub_backend_t *backend,
    robusto_proxy_pubsub_server_adapter_t *adapter,
    robusto_proxy_pubsub_subscription_t *subscriptions,
    uint16_t subscription_capacity,
    uint8_t *event_pool,
    uint32_t event_pool_capacity)
{
    robusto_proxy_pubsub_lock_t lock;

    if (backend == NULL)
    {
        return false;
    }
    backend->mutex = xSemaphoreCreateMutexStatic(&backend->mutex_storage);
    if (backend->mutex == NULL)
    {
        return false;
    }
    lock.take = backend_lock_take;
    lock.give = backend_lock_give;
    lock.context = backend;
    if (!robusto_proxy_pubsub_server_adapter_init(
            adapter, &backend_operations, backend, subscriptions,
            subscription_capacity, event_pool, event_pool_capacity, lock))
    {
        robusto_c6_pubsub_backend_deinit(backend);
        return false;
    }
    return true;
}

void robusto_c6_pubsub_backend_deinit(robusto_c6_pubsub_backend_t *backend)
{
    if (backend != NULL && backend->mutex != NULL)
    {
        vSemaphoreDelete(backend->mutex);
        backend->mutex = NULL;
    }
}