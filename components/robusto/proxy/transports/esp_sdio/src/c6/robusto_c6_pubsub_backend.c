#include "robusto_c6_pubsub_backend.h"

#include <string.h>

#include "esp_log.h"
#include "robusto_message.h"
#include "robusto_peer.h"
#include "robusto_proxy_protocol.h"
#include "robusto_pubsub_client.h"
#include "robusto_pubsub_server.h"
#include "robusto_retval.h"
#include "robusto_time.h"

static const char *TAG = "c6_pubsub";
#define ESPNOW_ROUTE_PREFIX "$robusto/espnow/"
#define ESPNOW_ROUTE_PREFIX_LENGTH (sizeof(ESPNOW_ROUTE_PREFIX) - 1U)
#define ESPNOW_ROUTE_CAPACITY 8U
#define ESPNOW_ROUTE_PEER_NAME_CAPACITY 32U

typedef struct robusto_c6_espnow_route {
    subscribed_topic_t *remote_topic;
    robusto_proxy_pubsub_local_callback_t delivery_callback;
    void *delivery_context;
    bool active;
} robusto_c6_espnow_route_t;

typedef struct robusto_c6_espnow_target {
    uint8_t mac[6];
    char peer_name[ESPNOW_ROUTE_PEER_NAME_CAPACITY];
    const char *topic;
} robusto_c6_espnow_target_t;

static robusto_c6_espnow_route_t s_espnow_routes[ESPNOW_ROUTE_CAPACITY];
static robusto_c6_pubsub_topic_hook_t s_subscribe_hook = NULL;
static robusto_c6_pubsub_topic_hook_t s_unsubscribe_hook = NULL;
static void *s_hook_context = NULL;

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parse_espnow_route(const char *route, robusto_c6_espnow_target_t *target)
{
    const char *cursor;
    const char *name_end;
    size_t name_length;

    if (route == NULL || target == NULL ||
        strncmp(route, ESPNOW_ROUTE_PREFIX, ESPNOW_ROUTE_PREFIX_LENGTH) != 0)
    {
        return false;
    }
    cursor = route + ESPNOW_ROUTE_PREFIX_LENGTH;
    for (size_t index = 0U; index < sizeof(target->mac); ++index)
    {
        int high = hex_nibble(cursor[index * 2U]);
        int low = hex_nibble(cursor[index * 2U + 1U]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        target->mac[index] = (uint8_t)((high << 4) | low);
    }
    cursor += 12U;
    if (*cursor++ != '/')
    {
        return false;
    }
    name_end = strchr(cursor, '/');
    if (name_end == NULL)
    {
        return false;
    }
    name_length = (size_t)(name_end - cursor);
    if (name_length == 0U || name_length >= sizeof(target->peer_name) || name_end[1] == '\0')
    {
        return false;
    }
    memcpy(target->peer_name, cursor, name_length);
    target->peer_name[name_length] = '\0';
    target->topic = name_end + 1;
    return true;
}

static robusto_peer_t *resolve_espnow_peer(const robusto_c6_espnow_target_t *target)
{
    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address_silent(
        (rob_mac_address *)target->mac);

    if (peer == NULL)
    {
        peer = add_peer_by_mac_address((char *)target->peer_name,
                           target->mac,
                           robusto_mt_espnow);
        if (peer == NULL)
        {
            ESP_LOGE(TAG, "ESP-NOW proxy peer add failed name=%s", target->peer_name);
            return NULL;
        }
        ESP_LOGI(TAG,
                 "ESP-NOW proxy peer added name=%s mac=%02X:%02X:%02X:%02X:%02X:%02X",
                 target->peer_name,
                 target->mac[0], target->mac[1], target->mac[2],
                 target->mac[3], target->mac[4], target->mac[5]);
    }
    return peer;
}

#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
static bool topic_request_accepted(const subscribed_topic_t *topic)
{
    return topic != NULL && topic->topic_hash != 0U &&
           topic->state != TOPIC_STATE_PROBLEM &&
           topic->state != TOPIC_STATE_UNKNOWN;
}

static void espnow_remote_delivery(subscribed_topic_t *topic,
                                   uint8_t *data,
                                   uint32_t data_length)
{
    for (size_t index = 0U; index < ESPNOW_ROUTE_CAPACITY; ++index)
    {
        robusto_c6_espnow_route_t *route = &s_espnow_routes[index];
        if (route->active && route->remote_topic == topic)
        {
            uint16_t status = route->delivery_callback(route->delivery_context,
                                                       data, data_length);
            if (status != ROBUSTO_PROXY_STATUS_OK)
            {
                ESP_LOGE(TAG, "ESP-NOW proxy delivery failed topic=%s status=0x%04X",
                         topic->topic_name, (unsigned)status);
            }
            return;
        }
    }
    ESP_LOGE(TAG, "ESP-NOW proxy delivery has no route topic=%s",
             topic != NULL && topic->topic_name != NULL ? topic->topic_name : "<null>");
}
#endif

void robusto_c6_pubsub_backend_set_topic_hooks(
    robusto_c6_pubsub_topic_hook_t subscribe_hook,
    robusto_c6_pubsub_topic_hook_t unsubscribe_hook,
    void *hook_context)
{
    s_subscribe_hook = subscribe_hook;
    s_unsubscribe_hook = unsubscribe_hook;
    s_hook_context = hook_context;
}

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
        case ROB_ERR_NOT_READY:
            return ROBUSTO_PROXY_STATUS_NOT_READY;
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
    robusto_c6_espnow_target_t target;
    pubsub_server_topic_t *robusto_topic;
    rob_ret_val_t result;

    (void)context;
    if (topic == NULL || (data_length > 0U && data == NULL) ||
        topic_hash == NULL || delivery_count == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (parse_espnow_route(topic, &target))
    {
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
        robusto_peer_t *peer = resolve_espnow_peer(&target);
        subscribed_topic_t *remote_topic;
        if (peer == NULL)
        {
            return ROBUSTO_PROXY_STATUS_NOT_READY;
        }
        remote_topic = robusto_pubsub_client_get_topic(peer, (char *)target.topic, NULL, 0);
        if (!topic_request_accepted(remote_topic))
        {
            ESP_LOGE(TAG, "ESP-NOW proxy publish topic lookup failed peer=%s topic=%s",
                     target.peer_name, target.topic);
            return ROBUSTO_PROXY_STATUS_NOT_READY;
        }
        result = robusto_pubsub_client_publish(remote_topic, (uint8_t *)data, data_length);
        *topic_hash = robusto_crc32(0, (const uint8_t *)topic, strlen(topic));
        *delivery_count = 0U;
        if (result == ROB_OK)
        {
            ESP_LOGI(TAG, "ESP-NOW proxy published peer=%s topic=%s bytes=%lu",
                     target.peer_name, target.topic, (unsigned long)data_length);
        }
        return map_robusto_publish_result(result);
    #else
        ESP_LOGE(TAG, "ESP-NOW proxy publish unavailable without PubSub client");
        return ROBUSTO_PROXY_STATUS_NOT_READY;
    #endif
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
    uint16_t status = map_robusto_publish_result(result);
    if (result != ROB_OK)
    {
        ESP_LOGE(TAG,
                 "Backend publish failed topic=%s bytes=%lu subscribers=%lu backend_rc=%d mapped_status=0x%04x",
                 topic,
                 (unsigned long)data_length,
                 (unsigned long)robusto_topic->subscriber_count,
                 (int)result,
                 (unsigned)status);
    }
    return status;
}

static uint16_t backend_subscribe(void *context,
                                  const char *topic,
                                  robusto_proxy_pubsub_local_callback_t callback,
                                  void *callback_context,
                                  uint32_t *topic_hash)
{
    robusto_c6_espnow_target_t target;
    robusto_proxy_pubsub_subscription_t *subscription = callback_context;
    uint32_t hash;

    (void)context;
    if (topic == NULL || callback == NULL || subscription == NULL ||
        topic_hash == NULL || subscription->delivery_callback != callback ||
        subscription->delivery_callback_context != callback_context)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (parse_espnow_route(topic, &target))
    {
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
        robusto_peer_t *peer = resolve_espnow_peer(&target);
        robusto_c6_espnow_route_t *route = NULL;
        if (peer == NULL)
        {
            return ROBUSTO_PROXY_STATUS_NOT_READY;
        }
        for (size_t index = 0U; index < ESPNOW_ROUTE_CAPACITY; ++index)
        {
            if (!s_espnow_routes[index].active)
            {
                route = &s_espnow_routes[index];
                break;
            }
        }
        if (route == NULL)
        {
            return ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_LIMIT;
        }
        route->delivery_callback = callback;
        route->delivery_context = callback_context;
        route->remote_topic = robusto_pubsub_client_get_topic(
            peer, (char *)target.topic, espnow_remote_delivery, 0);
        if (!topic_request_accepted(route->remote_topic))
        {
            ESP_LOGE(TAG, "ESP-NOW proxy subscribe failed peer=%s topic=%s",
                     target.peer_name, target.topic);
            memset(route, 0, sizeof(*route));
            return ROBUSTO_PROXY_STATUS_NOT_READY;
        }
        route->active = true;
        *topic_hash = robusto_crc32(0, (const uint8_t *)topic, strlen(topic));
        ESP_LOGI(TAG, "ESP-NOW proxy subscribed peer=%s topic=%s hash=%lu",
                 target.peer_name, target.topic, (unsigned long)*topic_hash);
        return ROBUSTO_PROXY_STATUS_OK;
        #else
            ESP_LOGE(TAG, "ESP-NOW proxy subscribe unavailable without PubSub client");
            return ROBUSTO_PROXY_STATUS_NOT_READY;
        #endif
    }
    if (s_subscribe_hook != NULL)
    {
        uint16_t hook_status = s_subscribe_hook(s_hook_context, topic);
        if (hook_status != ROBUSTO_PROXY_STATUS_OK)
        {
            return hook_status;
        }
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
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
    for (size_t index = 0U; index < ESPNOW_ROUTE_CAPACITY; ++index)
    {
        robusto_c6_espnow_route_t *route = &s_espnow_routes[index];
        if (route->active && route->delivery_context == callback_context)
        {
            rob_ret_val_t result = robusto_pubsub_client_unsubscribe(route->remote_topic);
            if (result != ROB_OK)
            {
                ESP_LOGE(TAG, "ESP-NOW proxy unsubscribe failed: %d", result);
                return map_robusto_publish_result(result);
            }
            memset(route, 0, sizeof(*route));
            return ROBUSTO_PROXY_STATUS_OK;
        }
    }
#endif
    if (s_unsubscribe_hook != NULL)
    {
        uint16_t hook_status = s_unsubscribe_hook(s_hook_context,
                                                  subscription->topic);
        if (hook_status != ROBUSTO_PROXY_STATUS_OK)
        {
            return hook_status;
        }
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
    memset(s_espnow_routes, 0, sizeof(s_espnow_routes));
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