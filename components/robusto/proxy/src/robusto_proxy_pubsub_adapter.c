#include "robusto_proxy_pubsub_adapter.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static uint8_t *allocate_publish_data(uint32_t data_length)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(data_length,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return malloc(data_length);
#endif
}

static uint8_t *allocate_delivery_data(uint32_t data_length)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(data_length, MALLOC_CAP_8BIT);
#else
    return malloc(data_length);
#endif
}

static bool adapter_take(robusto_proxy_pubsub_server_adapter_t *adapter)
{
    return adapter->lock.take == NULL || adapter->lock.take(adapter->lock.context);
}

static void adapter_give(robusto_proxy_pubsub_server_adapter_t *adapter)
{
    if (adapter->lock.give != NULL)
    {
        adapter->lock.give(adapter->lock.context);
    }
}

static robusto_proxy_pubsub_subscription_t *find_topic(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    const uint8_t *topic,
    uint16_t topic_length)
{
    for (uint16_t index = 0U; index < adapter->subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_subscription_t *subscription = &adapter->subscriptions[index];
        if (subscription->active && subscription->topic_length == topic_length &&
            memcmp(subscription->topic, topic, topic_length) == 0)
        {
            return subscription;
        }
    }
    return NULL;
}

static robusto_proxy_pubsub_subscription_t *find_id(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    uint32_t subscription_id)
{
    for (uint16_t index = 0U; index < adapter->subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_subscription_t *subscription = &adapter->subscriptions[index];
        if (subscription->active && subscription->subscription_id == subscription_id)
        {
            return subscription;
        }
    }
    return NULL;
}

static robusto_proxy_pubsub_subscription_t *find_free(
    robusto_proxy_pubsub_server_adapter_t *adapter)
{
    for (uint16_t index = 0U; index < adapter->subscription_capacity; ++index)
    {
        if (!adapter->subscriptions[index].active)
        {
            return &adapter->subscriptions[index];
        }
    }
    return NULL;
}

static uint32_t take_subscription_id(robusto_proxy_pubsub_server_adapter_t *adapter)
{
    uint32_t candidate = adapter->next_subscription_id == 0U ? 1U : adapter->next_subscription_id;
    const uint32_t first_candidate = candidate;
    do
    {
        if (find_id(adapter, candidate) == NULL)
        {
            adapter->next_subscription_id = candidate + 1U;
            if (adapter->next_subscription_id == 0U)
            {
                adapter->next_subscription_id = 1U;
            }
            return candidate;
        }
        ++candidate;
        if (candidate == 0U)
        {
            candidate = 1U;
        }
    } while (candidate != first_candidate);
    return 0U;
}

static uint16_t queue_delivery(void *context, const uint8_t *data, uint32_t data_length)
{
    robusto_proxy_pubsub_subscription_t *subscription = context;
    robusto_proxy_pubsub_server_adapter_t *adapter;
    robusto_proxy_pubsub_event_descriptor_t *event;
    uint32_t sequence;

    if (subscription == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    adapter = subscription->adapter;
    if (adapter == NULL || (data_length > 0U && data == NULL))
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    sequence = subscription->next_delivery_sequence++;
    if (subscription->next_delivery_sequence == 0U)
    {
        subscription->next_delivery_sequence = 1U;
    }
    if (!subscription->active ||
        adapter->event_count == ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT ||
        (data_length <= ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES &&
         data_length > adapter->event_pool_capacity - adapter->event_pool_used))
    {
        adapter->delivery_drops += 1U;
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_OK;
    }
    event = &adapter->events[adapter->event_write_index];
    memset(event, 0, sizeof(*event));
    event->subscription_id = subscription->subscription_id;
    event->delivery_sequence = sequence;
    event->data_length = data_length;
    if (data_length > ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES)
    {
        if (data == adapter->publish_dispatch_data &&
            data_length == adapter->publish_dispatch_data_length &&
            !adapter->publish_dispatch_transferred)
        {
            event->transfer_data = adapter->publish_dispatch_data;
            adapter->publish_dispatch_transferred = true;
        }
        else
        {
            event->transfer_data = allocate_delivery_data(data_length);
            if (event->transfer_data == NULL)
            {
                adapter->delivery_drops += 1U;
                adapter_give(adapter);
                return ROBUSTO_PROXY_STATUS_OK;
            }
            memcpy(event->transfer_data, data, data_length);
        }
    }
    else
    {
        event->pool_offset = adapter->event_pool_write;
        for (uint32_t index = 0U; index < data_length; ++index)
        {
            adapter->event_pool[adapter->event_pool_write] = data[index];
            adapter->event_pool_write =
                (adapter->event_pool_write + 1U) % adapter->event_pool_capacity;
        }
        adapter->event_pool_used += data_length;
    }
    adapter->event_write_index = (uint8_t)((adapter->event_write_index + 1U) %
                                           ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT);
    adapter->event_count += 1U;
    adapter->delivery_events += 1U;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_publish(void *context,
                                const robusto_proxy_pubsub_publish_request_t *request,
                                robusto_proxy_pubsub_publish_response_t *response)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;
    char topic[ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U];
    uint16_t status;

    if (adapter == NULL || request == NULL || response == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    memcpy(topic, request->topic, request->topic_length);
    topic[request->topic_length] = '\0';
    adapter->publish_requests += 1U;
    status = adapter->backend->publish(adapter->backend_context, topic,
                                       request->data, request->data_length,
                                       &response->topic_hash, &response->delivery_count);
    if (status != ROBUSTO_PROXY_STATUS_OK)
    {
        adapter->pubsub_errors += 1U;
    }
    return status;
}

static void release_publish_transfer(robusto_proxy_pubsub_server_adapter_t *adapter)
{
    free(adapter->publish_data);
    adapter->publish_data = NULL;
    adapter->publish_operation_id = 0U;
    adapter->publish_data_length = 0U;
    adapter->publish_data_received = 0U;
    adapter->publish_topic_length = 0U;
    adapter->publish_topic[0] = '\0';
}

static uint16_t adapter_publish_begin(
    void *context, const robusto_proxy_pubsub_publish_begin_request_t *request)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;

    if (adapter == NULL || request == NULL || request->operation_id == 0U ||
        request->data_length == 0U ||
        !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    if (adapter->publish_data != NULL || adapter->publish_dispatch_data != NULL)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    adapter->publish_data = allocate_publish_data(request->data_length);
    if (adapter->publish_data == NULL)
    {
        adapter->pubsub_errors += 1U;
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY;
    }
    adapter->publish_operation_id = request->operation_id;
    adapter->publish_data_length = request->data_length;
    adapter->publish_data_received = 0U;
    adapter->publish_topic_length = request->topic_length;
    memcpy(adapter->publish_topic, request->topic, request->topic_length);
    adapter->publish_topic[request->topic_length] = '\0';
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_publish_chunk(
    void *context, const robusto_proxy_pubsub_publish_chunk_request_t *request)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;

    if (adapter == NULL || request == NULL || request->operation_id == 0U ||
        request->data == NULL || request->data_length == 0U)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    if (adapter->publish_data == NULL ||
        adapter->publish_operation_id != request->operation_id)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_CONFLICT;
    }
    if (request->offset != adapter->publish_data_received ||
        request->data_length >
            adapter->publish_data_length - adapter->publish_data_received)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
    }
    memcpy(adapter->publish_data + request->offset, request->data,
           request->data_length);
    adapter->publish_data_received += request->data_length;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_publish_commit(
    void *context, const robusto_proxy_pubsub_publish_transfer_request_t *request,
    robusto_proxy_pubsub_publish_response_t *response)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;
    robusto_proxy_pubsub_publish_request_t publish_request;
    uint8_t *publish_data;
    uint32_t publish_data_length;
    uint16_t publish_topic_length;
    char publish_topic[ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U];
    uint16_t status;

    if (adapter == NULL || request == NULL || response == NULL ||
        request->operation_id == 0U)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    if (adapter->publish_data == NULL ||
        adapter->publish_operation_id != request->operation_id)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_CONFLICT;
    }
    if (adapter->publish_data_received != adapter->publish_data_length)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
    }
    publish_data = adapter->publish_data;
    publish_data_length = adapter->publish_data_length;
    publish_topic_length = adapter->publish_topic_length;
    memcpy(publish_topic, adapter->publish_topic, publish_topic_length + 1U);
    adapter->publish_data = NULL;
    adapter->publish_operation_id = 0U;
    adapter->publish_data_length = 0U;
    adapter->publish_data_received = 0U;
    adapter->publish_topic_length = 0U;
    adapter->publish_topic[0] = '\0';
    adapter->publish_dispatch_data = publish_data;
    adapter->publish_dispatch_data_length = publish_data_length;
    adapter->publish_dispatch_transferred = false;
    adapter_give(adapter);

    memset(&publish_request, 0, sizeof(publish_request));
    publish_request.operation_id = request->operation_id;
    publish_request.topic = (const uint8_t *)publish_topic;
    publish_request.topic_length = publish_topic_length;
    publish_request.data = publish_data;
    publish_request.data_length = publish_data_length;
    status = adapter_publish(adapter, &publish_request, response);
    bool transferred = adapter->publish_dispatch_transferred;
    adapter->publish_dispatch_data = NULL;
    adapter->publish_dispatch_data_length = 0U;
    adapter->publish_dispatch_transferred = false;
    if (!transferred)
    {
        free(publish_data);
    }
    return status;
}

static uint16_t adapter_publish_abort(
    void *context, const robusto_proxy_pubsub_publish_transfer_request_t *request)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;

    if (adapter == NULL || request == NULL || request->operation_id == 0U)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    if (adapter->publish_data == NULL ||
        adapter->publish_operation_id != request->operation_id)
    {
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_CONFLICT;
    }
    release_publish_transfer(adapter);
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_session_reset(void *context)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;

    if (adapter == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    release_publish_transfer(adapter);
    while (adapter->event_count > 0U)
    {
        robusto_proxy_pubsub_event_descriptor_t *event =
            &adapter->events[adapter->event_read_index];
        if (event->transfer_data != NULL)
        {
            free(event->transfer_data);
        }
        else
        {
            adapter->event_pool_read =
                (adapter->event_pool_read + event->data_length) %
                adapter->event_pool_capacity;
            adapter->event_pool_used -= event->data_length;
        }
        memset(event, 0, sizeof(*event));
        adapter->event_read_index = (uint8_t)((adapter->event_read_index + 1U) %
                                              ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT);
        adapter->event_count -= 1U;
    }
    adapter->pending_delivery_opcode = 0U;
    adapter->pending_delivery_chunk_length = 0U;
    adapter->delivery_pending = false;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_subscribe(void *context,
                                  const robusto_proxy_pubsub_subscribe_request_t *request,
                                  robusto_proxy_pubsub_subscribe_response_t *response)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;
    robusto_proxy_pubsub_subscription_t *subscription;
    uint16_t status;

    if (adapter == NULL || request == NULL || response == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    adapter->subscribe_requests += 1U;
    subscription = find_topic(adapter, request->topic, request->topic_length);
    if (subscription != NULL)
    {
        response->subscription_id = subscription->subscription_id;
        response->topic_hash = subscription->topic_hash;
        response->created = 0U;
        adapter->duplicate_operations += 1U;
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_OK;
    }
    subscription = find_free(adapter);
    if (subscription == NULL)
    {
        adapter->pubsub_errors += 1U;
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_LIMIT;
    }
    memset(subscription, 0, sizeof(*subscription));
    subscription->adapter = adapter;
    subscription->delivery_callback = queue_delivery;
    subscription->delivery_callback_context = subscription;
    subscription->subscription_id = take_subscription_id(adapter);
    subscription->next_delivery_sequence = 1U;
    subscription->topic_length = request->topic_length;
    memcpy(subscription->topic, request->topic, request->topic_length);
    subscription->active = true;
    status = adapter->backend->subscribe(adapter->backend_context, subscription->topic,
                                         queue_delivery, subscription,
                                         &subscription->topic_hash);
    if (status != ROBUSTO_PROXY_STATUS_OK)
    {
        memset(subscription, 0, sizeof(*subscription));
        adapter->pubsub_errors += 1U;
        adapter_give(adapter);
        return status;
    }
    adapter->active_subscriptions += 1U;
    response->subscription_id = subscription->subscription_id;
    response->topic_hash = subscription->topic_hash;
    response->created = 1U;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_unsubscribe(void *context,
                                    const robusto_proxy_pubsub_unsubscribe_request_t *request,
                                    robusto_proxy_pubsub_unsubscribe_response_t *response)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;
    robusto_proxy_pubsub_subscription_t *subscription;
    uint16_t status;

    if (adapter == NULL || request == NULL || response == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    adapter->unsubscribe_requests += 1U;
    subscription = find_id(adapter, request->subscription_id);
    if (subscription == NULL)
    {
        response->removed = 0U;
        adapter_give(adapter);
        return ROBUSTO_PROXY_STATUS_OK;
    }
    status = adapter->backend->unsubscribe(adapter->backend_context,
                                           subscription->topic_hash,
                                           queue_delivery, subscription);
    if (status != ROBUSTO_PROXY_STATUS_OK)
    {
        adapter->pubsub_errors += 1U;
        adapter_give(adapter);
        return status;
    }
    subscription->active = false;
    adapter->active_subscriptions -= 1U;
    response->removed = 1U;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t adapter_status(void *context,
                               robusto_proxy_pubsub_status_response_t *response)
{
    robusto_proxy_pubsub_server_adapter_t *adapter = context;
    if (adapter == NULL || response == NULL)
    {
        return ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT;
    }
    if (!adapter_take(adapter))
    {
        return ROBUSTO_PROXY_STATUS_BUSY;
    }
    memset(response, 0, sizeof(*response));
    response->state = 1U;
    response->active_subscriptions = adapter->active_subscriptions;
    response->publish_requests = adapter->publish_requests;
    response->subscribe_requests = adapter->subscribe_requests;
    response->unsubscribe_requests = adapter->unsubscribe_requests;
    response->delivery_events = adapter->delivery_events;
    response->delivery_drops = adapter->delivery_drops;
    response->duplicate_operations = adapter->duplicate_operations;
    response->pubsub_errors = adapter->pubsub_errors;
    adapter_give(adapter);
    return ROBUSTO_PROXY_STATUS_OK;
}

static const robusto_proxy_pubsub_adapter_t operations = {
    .publish = adapter_publish,
    .publish_begin = adapter_publish_begin,
    .publish_chunk = adapter_publish_chunk,
    .publish_commit = adapter_publish_commit,
    .publish_abort = adapter_publish_abort,
    .session_reset = adapter_session_reset,
    .subscribe = adapter_subscribe,
    .unsubscribe = adapter_unsubscribe,
    .status = adapter_status,
};

bool robusto_proxy_pubsub_server_adapter_init(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    const robusto_proxy_pubsub_backend_t *backend,
    void *backend_context,
    robusto_proxy_pubsub_subscription_t *subscriptions,
    uint16_t subscription_capacity,
    uint8_t *event_pool,
    uint32_t event_pool_capacity,
    robusto_proxy_pubsub_lock_t lock)
{
    if (adapter == NULL || backend == NULL || backend->publish == NULL ||
        backend->subscribe == NULL || backend->unsubscribe == NULL ||
        subscriptions == NULL || subscription_capacity == 0U ||
        subscription_capacity > 32U || event_pool == NULL || event_pool_capacity == 0U ||
        (lock.take == NULL) != (lock.give == NULL))
    {
        return false;
    }
    memset(adapter, 0, sizeof(*adapter));
    memset(subscriptions, 0, sizeof(*subscriptions) * subscription_capacity);
    adapter->backend = backend;
    adapter->backend_context = backend_context;
    adapter->subscriptions = subscriptions;
    adapter->subscription_capacity = subscription_capacity;
    adapter->event_pool = event_pool;
    adapter->event_pool_capacity = event_pool_capacity;
    adapter->next_subscription_id = 1U;
    adapter->lock = lock;
    return true;
}

bool robusto_proxy_pubsub_server_adapter_deinit(
    robusto_proxy_pubsub_server_adapter_t *adapter)
{
    bool success = true;

    if (adapter == NULL || adapter->backend == NULL ||
        adapter->backend->unsubscribe == NULL || adapter->subscriptions == NULL)
    {
        return false;
    }
    if (!adapter_take(adapter))
    {
        return false;
    }
    for (uint16_t index = 0U; index < adapter->subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_subscription_t *subscription =
            &adapter->subscriptions[index];
        uint16_t status;

        if (!subscription->active)
        {
            continue;
        }
        status = adapter->backend->unsubscribe(
            adapter->backend_context, subscription->topic_hash,
            subscription->delivery_callback,
            subscription->delivery_callback_context);
        if (status == ROBUSTO_PROXY_STATUS_OK)
        {
            memset(subscription, 0, sizeof(*subscription));
            adapter->active_subscriptions -= 1U;
        }
        else
        {
            success = false;
            adapter->pubsub_errors += 1U;
        }
    }
    release_publish_transfer(adapter);
    while (adapter->event_count > 0U)
    {
        robusto_proxy_pubsub_event_descriptor_t *event =
            &adapter->events[adapter->event_read_index];
        free(event->transfer_data);
        memset(event, 0, sizeof(*event));
        adapter->event_read_index = (uint8_t)((adapter->event_read_index + 1U) %
                                              ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT);
        adapter->event_count -= 1U;
    }
    adapter->event_pool_read = 0U;
    adapter->event_pool_write = 0U;
    adapter->event_pool_used = 0U;
    adapter->pending_delivery_opcode = 0U;
    adapter->pending_delivery_chunk_length = 0U;
    adapter->delivery_pending = false;
    adapter_give(adapter);
    return success;
}

const robusto_proxy_pubsub_adapter_t *robusto_proxy_pubsub_server_adapter_operations(void)
{
    return &operations;
}

bool robusto_proxy_pubsub_server_adapter_take_delivery(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    bool chunked_delivery_enabled,
    uint8_t *opcode,
    uint8_t *payload_buffer,
    size_t payload_buffer_size,
    size_t *payload_size)
{
    robusto_proxy_pubsub_event_descriptor_t event;
    robusto_proxy_pubsub_delivery_t delivery;
    robusto_proxy_result_t result;

    if (adapter == NULL || opcode == NULL || payload_buffer == NULL ||
        payload_size == NULL || !adapter_take(adapter))
    {
        return false;
    }
    if (adapter->event_count == 0U || adapter->delivery_pending)
    {
        adapter_give(adapter);
        return false;
    }
    event = adapter->events[adapter->event_read_index];
    if (event.transfer_data != NULL)
    {
        if (!chunked_delivery_enabled)
        {
            free(event.transfer_data);
            memset(&adapter->events[adapter->event_read_index], 0,
                   sizeof(adapter->events[adapter->event_read_index]));
            adapter->event_read_index = (uint8_t)((adapter->event_read_index + 1U) %
                                                  ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT);
            adapter->event_count -= 1U;
            adapter->delivery_drops += 1U;
            adapter_give(adapter);
            return false;
        }
        if (event.transfer_stage == 0U)
        {
            robusto_proxy_pubsub_delivery_begin_t begin = {
                event.subscription_id, event.delivery_sequence, event.data_length};
            result = robusto_proxy_pubsub_encode_delivery_begin(
                payload_buffer, payload_buffer_size, &begin);
            if (result == ROBUSTO_PROXY_RESULT_OK)
            {
                *opcode = ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_BEGIN;
                *payload_size = ROBUSTO_PROXY_PUBSUB_DELIVERY_BEGIN_SIZE_BYTES;
                adapter->pending_delivery_opcode = *opcode;
                adapter->pending_delivery_chunk_length = 0U;
                adapter->delivery_pending = true;
            }
            adapter_give(adapter);
            return result == ROBUSTO_PROXY_RESULT_OK;
        }
        if (event.transfer_stage == 1U)
        {
            uint32_t remaining = event.data_length - event.transfer_offset;
            uint32_t chunk_capacity;
            uint32_t chunk_length;
            if (payload_buffer_size <= ROBUSTO_PROXY_PUBSUB_DELIVERY_CHUNK_HEADER_SIZE_BYTES)
            {
                adapter_give(adapter);
                return false;
            }
            chunk_capacity = (uint32_t)(payload_buffer_size -
                ROBUSTO_PROXY_PUBSUB_DELIVERY_CHUNK_HEADER_SIZE_BYTES);
            if (chunk_capacity > ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_CHUNK_DATA_BYTES)
            {
                chunk_capacity = ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_CHUNK_DATA_BYTES;
            }
            chunk_length = remaining > chunk_capacity ? chunk_capacity : remaining;
            robusto_proxy_pubsub_delivery_chunk_t chunk = {
                event.subscription_id, event.delivery_sequence,
                event.transfer_offset, event.transfer_data + event.transfer_offset,
                chunk_length};
            result = robusto_proxy_pubsub_encode_delivery_chunk(
                payload_buffer, payload_buffer_size, &chunk, payload_size);
            if (result == ROBUSTO_PROXY_RESULT_OK)
            {
                *opcode = ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK;
                adapter->pending_delivery_opcode = *opcode;
                adapter->pending_delivery_chunk_length = chunk_length;
                adapter->delivery_pending = true;
            }
            adapter_give(adapter);
            return result == ROBUSTO_PROXY_RESULT_OK;
        }
        {
            robusto_proxy_pubsub_delivery_commit_t commit = {
                event.subscription_id, event.delivery_sequence};
            result = robusto_proxy_pubsub_encode_delivery_commit(
                payload_buffer, payload_buffer_size, &commit);
            if (result != ROBUSTO_PROXY_RESULT_OK)
            {
                adapter_give(adapter);
                return false;
            }
            *opcode = ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_COMMIT;
            *payload_size = ROBUSTO_PROXY_PUBSUB_DELIVERY_COMMIT_SIZE_BYTES;
                 adapter->pending_delivery_opcode = *opcode;
                 adapter->pending_delivery_chunk_length = 0U;
                 adapter->delivery_pending = true;
            adapter_give(adapter);
            return true;
        }
    }
    if (payload_buffer_size < ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES + event.data_length)
    {
        adapter_give(adapter);
        return false;
    }
    for (uint32_t index = 0U; index < event.data_length; ++index)
    {
        payload_buffer[ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES + index] =
            adapter->event_pool[(event.pool_offset + index) % adapter->event_pool_capacity];
    }
    delivery.subscription_id = event.subscription_id;
    delivery.delivery_sequence = event.delivery_sequence;
    delivery.data_length = event.data_length;
    delivery.data = payload_buffer + ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES;
    if (robusto_proxy_pubsub_encode_delivery(payload_buffer, payload_buffer_size,
                                              &delivery, payload_size) != ROBUSTO_PROXY_RESULT_OK)
    {
        adapter_give(adapter);
        return false;
    }
    *opcode = ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY;
    adapter->pending_delivery_opcode = *opcode;
    adapter->pending_delivery_chunk_length = 0U;
    adapter->delivery_pending = true;
    adapter_give(adapter);
    return true;
}

bool robusto_proxy_pubsub_server_adapter_complete_delivery(
    robusto_proxy_pubsub_server_adapter_t *adapter,
    bool sent)
{
    robusto_proxy_pubsub_event_descriptor_t *event;

    if (adapter == NULL || !adapter_take(adapter))
    {
        return false;
    }
    if (!adapter->delivery_pending || adapter->event_count == 0U)
    {
        adapter_give(adapter);
        return false;
    }
    event = &adapter->events[adapter->event_read_index];
    if (sent)
    {
        if (adapter->pending_delivery_opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_BEGIN)
        {
            event->transfer_stage = 1U;
        }
        else if (adapter->pending_delivery_opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK)
        {
            event->transfer_offset += adapter->pending_delivery_chunk_length;
            if (event->transfer_offset == event->data_length)
            {
                event->transfer_stage = 2U;
            }
        }
        else
        {
            if (event->transfer_data != NULL)
            {
                free(event->transfer_data);
            }
            else
            {
                adapter->event_pool_read =
                    (adapter->event_pool_read + event->data_length) %
                    adapter->event_pool_capacity;
                adapter->event_pool_used -= event->data_length;
            }
            memset(event, 0, sizeof(*event));
            adapter->event_read_index =
                (uint8_t)((adapter->event_read_index + 1U) %
                          ROBUSTO_PROXY_PUBSUB_EVENT_DESCRIPTOR_LIMIT);
            adapter->event_count -= 1U;
        }
    }
    adapter->pending_delivery_opcode = 0U;
    adapter->pending_delivery_chunk_length = 0U;
    adapter->delivery_pending = false;
    adapter_give(adapter);
    return true;
}
