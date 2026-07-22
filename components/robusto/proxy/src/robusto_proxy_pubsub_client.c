#include "robusto_proxy_pubsub_client.h"

#include <string.h>

#include "robusto_proxy_frame.h"
#include "robusto_proxy_pubsub.h"

typedef struct robusto_proxy_pubsub_client_subscription_fields {
    robusto_proxy_client_t *client;
    robusto_proxy_pubsub_callback *callback;
    void *callback_context;
    uint32_t subscription_id;
    uint32_t topic_hash;
    uint32_t next_delivery_sequence;
    uint16_t topic_length;
    bool allocated;
    bool active;
    char topic[ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U];
} robusto_proxy_pubsub_client_subscription_fields_t;

_Static_assert(sizeof(robusto_proxy_pubsub_client_subscription_fields_t) <=
                   ROBUSTO_PROXY_PUBSUB_CLIENT_SUBSCRIPTION_STORAGE_BYTES,
               "Proxy PubSub subscription storage is too small");

static size_t bounded_string_length(const char *value, size_t maximum)
{
    size_t length = 0U;

    while (length < maximum && value[length] != '\0')
    {
        ++length;
    }
    return length;
}

static robusto_proxy_pubsub_client_subscription_fields_t *subscription_fields(
    robusto_proxy_pubsub_client_subscription_t *subscription)
{
    return (robusto_proxy_pubsub_client_subscription_fields_t *)subscription;
}

static robusto_proxy_pubsub_client_subscription_t *find_free_subscription(
    robusto_proxy_client_t *client)
{
    robusto_proxy_pubsub_client_subscription_t *subscriptions =
        client->pubsub_subscriptions;
    uint16_t index;

    for (index = 0U; index < client->pubsub_subscription_capacity; ++index)
    {
        if (!subscription_fields(&subscriptions[index])->allocated)
        {
            return &subscriptions[index];
        }
    }
    return NULL;
}

static robusto_proxy_pubsub_client_subscription_fields_t *find_subscription_by_id(
    robusto_proxy_client_t *client,
    uint32_t subscription_id)
{
    robusto_proxy_pubsub_client_subscription_t *subscriptions =
        client->pubsub_subscriptions;
    uint16_t index;

    if (subscriptions == NULL || subscription_id == 0U)
    {
        return NULL;
    }
    for (index = 0U; index < client->pubsub_subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_client_subscription_fields_t *fields =
            subscription_fields(&subscriptions[index]);
        if (fields->active && fields->subscription_id == subscription_id)
        {
            return fields;
        }
    }
    return NULL;
}

rob_ret_val_t robusto_proxy_pubsub_configure(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_client_subscription_t *subscriptions,
    uint16_t subscription_capacity)
{
    if (client == NULL || subscriptions == NULL || subscription_capacity == 0U ||
        subscription_capacity > client->session.local_limits.max_subscriptions)
    {
        return ROB_ERR_INVALID_ARG;
    }
    memset(subscriptions, 0, sizeof(*subscriptions) * subscription_capacity);
    client->pubsub_subscriptions = subscriptions;
    client->pubsub_subscription_capacity = subscription_capacity;
    return ROB_OK;
}

bool robusto_proxy_pubsub_is_ready(const robusto_proxy_client_t *client)
{
    return client != NULL &&
           client->session.state == ROBUSTO_PROXY_SESSION_ESTABLISHED &&
           (client->session.enabled_features & ROBUSTO_PROXY_FEATURE_PUBSUB_V1) != 0U;
}

rob_ret_val_t robusto_proxy_pubsub_subscribe(
    robusto_proxy_client_t *client,
    const char *topic_name,
    robusto_proxy_pubsub_callback *callback,
    void *callback_context,
    robusto_proxy_pubsub_client_subscription_t **subscription)
{
    robusto_proxy_pubsub_subscribe_request_t request;
    robusto_proxy_pubsub_subscribe_response_t response;
    robusto_proxy_pubsub_client_subscription_t *free_subscription;
    robusto_proxy_pubsub_client_subscription_fields_t *fields;
    const uint8_t *response_payload;
    size_t response_payload_size;
    size_t request_size;
    size_t topic_length;
    rob_ret_val_t result;

    if (client == NULL || topic_name == NULL || callback == NULL || subscription == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    *subscription = NULL;
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }
    if (client->pubsub_subscriptions == NULL)
    {
        return ROB_ERR_INIT_FAIL;
    }
    topic_length = bounded_string_length(
        topic_name, ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U);
    if (topic_length == 0U || topic_length > ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES)
    {
        return ROB_ERR_INVALID_ARG;
    }
    free_subscription = find_free_subscription(client);
    if (free_subscription == NULL)
    {
        return ROB_ERR_QUEUE_FULL;
    }

    memset(&request, 0, sizeof(request));
    request.operation_id = robusto_proxy_client_take_operation_id(client);
    request.topic = (const uint8_t *)topic_name;
    request.topic_length = (uint16_t)topic_length;
    request.options = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES;
    if (robusto_proxy_pubsub_encode_subscribe_request(
            client->response_frame, client->response_frame_size,
            &request, &request_size) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_INVALID_ARG;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB, ROBUSTO_PROXY_PUBSUB_OPCODE_SUBSCRIBE,
        client->response_frame, request_size, true,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_pubsub_decode_subscribe_response(
            response_payload, response_payload_size, &response) !=
            ROBUSTO_PROXY_RESULT_OK ||
        response.subscription_id == 0U)
    {
        return ROB_ERR_PARSING_FAILED;
    }

    fields = subscription_fields(free_subscription);
    memset(fields, 0, sizeof(*fields));
    fields->client = client;
    fields->callback = callback;
    fields->callback_context = callback_context;
    fields->subscription_id = response.subscription_id;
    fields->topic_hash = response.topic_hash;
    fields->next_delivery_sequence = 1U;
    fields->topic_length = (uint16_t)topic_length;
    fields->allocated = true;
    fields->active = true;
    memcpy(fields->topic, topic_name, topic_length + 1U);
    *subscription = free_subscription;
    return ROB_OK;
}

rob_ret_val_t robusto_proxy_pubsub_unsubscribe(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_client_subscription_t *subscription)
{
    robusto_proxy_pubsub_client_subscription_fields_t *fields;
    robusto_proxy_pubsub_unsubscribe_request_t request;
    robusto_proxy_pubsub_unsubscribe_response_t response;
    const uint8_t *response_payload;
    size_t response_payload_size;
    rob_ret_val_t result;

    if (client == NULL || subscription == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    fields = subscription_fields(subscription);
    if (!fields->allocated || fields->client != client)
    {
        return ROB_ERR_INVALID_ID;
    }
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }

    memset(&request, 0, sizeof(request));
    request.operation_id = robusto_proxy_client_take_operation_id(client);
    request.subscription_id = fields->subscription_id;
    if (robusto_proxy_pubsub_encode_unsubscribe_request(
            client->response_frame, client->response_frame_size, &request) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_FAIL;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB, ROBUSTO_PROXY_PUBSUB_OPCODE_UNSUBSCRIBE,
        client->response_frame, ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_REQUEST_SIZE_BYTES,
        true, &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_pubsub_decode_unsubscribe_response(
            response_payload, response_payload_size, &response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    (void)response;
    memset(fields, 0, sizeof(*fields));
    return ROB_OK;
}

static rob_ret_val_t abort_publish_transfer(
    robusto_proxy_client_t *client, uint64_t operation_id)
{
    robusto_proxy_pubsub_publish_transfer_request_t request;
    const uint8_t *response_payload;
    size_t response_payload_size;

    request.operation_id = operation_id;
    if (robusto_proxy_pubsub_encode_publish_transfer_request(
            client->response_frame, client->response_frame_size, &request) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_FAIL;
    }
    return robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
        ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_ABORT,
        client->response_frame,
        ROBUSTO_PROXY_PUBSUB_PUBLISH_TRANSFER_REQUEST_SIZE_BYTES, true,
        &response_payload, &response_payload_size);
}

static rob_ret_val_t finish_failed_publish(
    robusto_proxy_client_t *client, uint64_t operation_id,
    rob_ret_val_t publish_result)
{
    rob_ret_val_t abort_result = abort_publish_transfer(client, operation_id);

    if (abort_result != ROB_OK)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_DEGRADED;
        return ROB_ERR_OUTCOME_UNKNOWN;
    }
    return publish_result == ROB_ERR_OUTCOME_UNKNOWN ? ROB_FAIL : publish_result;
}

static rob_ret_val_t publish_chunked(
    robusto_proxy_client_t *client, const char *topic_name, size_t topic_length,
    uint8_t *data, uint32_t data_length)
{
    robusto_proxy_pubsub_publish_begin_request_t begin_request;
    robusto_proxy_pubsub_publish_chunk_request_t chunk_request;
    robusto_proxy_pubsub_publish_transfer_request_t commit_request;
    robusto_proxy_pubsub_publish_response_t response;
    const uint8_t *response_payload;
    size_t response_payload_size;
    size_t request_size;
    uint64_t operation_id = robusto_proxy_client_take_operation_id(client);
    uint32_t offset = 0U;
    rob_ret_val_t result;

    if ((client->session.enabled_features &
         ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH) == 0U)
    {
        return ROB_ERR_NOT_SUPPORTED;
    }

    memset(&begin_request, 0, sizeof(begin_request));
    begin_request.operation_id = operation_id;
    begin_request.topic = (const uint8_t *)topic_name;
    begin_request.topic_length = (uint16_t)topic_length;
    begin_request.data_length = data_length;
    if (robusto_proxy_pubsub_encode_publish_begin_request(
            client->response_frame, client->response_frame_size,
            &begin_request, &request_size) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_INVALID_ARG;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
        ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_BEGIN,
        client->response_frame, request_size, true,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        if (result == ROB_ERR_OUTCOME_UNKNOWN)
        {
            return finish_failed_publish(client, operation_id, result);
        }
        return result;
    }

    while (offset < data_length)
    {
        uint32_t remaining = data_length - offset;
        uint32_t chunk_length = remaining > ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES
                                    ? ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES
                                    : remaining;
        memset(&chunk_request, 0, sizeof(chunk_request));
        chunk_request.operation_id = operation_id;
        chunk_request.offset = offset;
        chunk_request.data = data + offset;
        chunk_request.data_length = chunk_length;
        if (robusto_proxy_pubsub_encode_publish_chunk_request(
                client->response_frame, client->response_frame_size,
                &chunk_request, &request_size) != ROBUSTO_PROXY_RESULT_OK)
        {
            result = ROB_FAIL;
            break;
        }
        result = robusto_proxy_client_request(
            client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
            ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_CHUNK,
            client->response_frame, request_size, true,
            &response_payload, &response_payload_size);
        if (result != ROB_OK)
        {
            break;
        }
        offset += chunk_length;
    }
    if (result != ROB_OK)
    {
        return finish_failed_publish(client, operation_id, result);
    }

    commit_request.operation_id = operation_id;
    if (robusto_proxy_pubsub_encode_publish_transfer_request(
            client->response_frame, client->response_frame_size,
            &commit_request) != ROBUSTO_PROXY_RESULT_OK)
    {
        return finish_failed_publish(client, operation_id, ROB_FAIL);
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
        ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_COMMIT,
        client->response_frame,
        ROBUSTO_PROXY_PUBSUB_PUBLISH_TRANSFER_REQUEST_SIZE_BYTES, true,
        &response_payload, &response_payload_size);
    if (result == ROB_ERR_OUTCOME_UNKNOWN)
    {
        return finish_failed_publish(client, operation_id, result);
    }
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_pubsub_decode_publish_response(
            response_payload, response_payload_size, &response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    return ROB_OK;
}

rob_ret_val_t robusto_proxy_pubsub_publish(
    robusto_proxy_client_t *client,
    const char *topic_name,
    uint8_t *data,
    uint32_t data_length)
{
    robusto_proxy_pubsub_publish_request_t request;
    robusto_proxy_pubsub_publish_response_t response;
    const uint8_t *response_payload;
    size_t response_payload_size;
    size_t request_size;
    size_t topic_length;
    rob_ret_val_t result;

    if (client == NULL || topic_name == NULL ||
        (data_length > 0U && data == NULL))
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }
    topic_length = bounded_string_length(
        topic_name, ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES + 1U);
    if (topic_length == 0U || topic_length > ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (data_length > ROBUSTO_PROXY_PUBSUB_MAX_INLINE_DATA_BYTES)
    {
        return publish_chunked(client, topic_name, topic_length, data, data_length);
    }

    memset(&request, 0, sizeof(request));
    request.operation_id = robusto_proxy_client_take_operation_id(client);
    request.topic = (const uint8_t *)topic_name;
    request.topic_length = (uint16_t)topic_length;
    request.data = data;
    request.data_length = data_length;
    if (robusto_proxy_pubsub_encode_publish_request(
            client->response_frame, client->response_frame_size,
            &request, &request_size) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_INVALID_ARG;
    }

    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB, ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH,
        client->response_frame, request_size, true,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_pubsub_decode_publish_response(
            response_payload, response_payload_size, &response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    return ROB_OK;
}

rob_ret_val_t robusto_proxy_pubsub_query_status(
    robusto_proxy_client_t *client,
    robusto_proxy_pubsub_status_response_t *response)
{
    const uint8_t *response_payload;
    size_t response_payload_size;
    rob_ret_val_t result;

    if (client == NULL || response == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
        ROBUSTO_PROXY_PUBSUB_OPCODE_STATUS, NULL, 0U, false,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_pubsub_decode_status_response(
            response_payload, response_payload_size, response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    return ROB_OK;
}

rob_ret_val_t robusto_proxy_pubsub_handle_event(
    robusto_proxy_client_t *client,
    const uint8_t *event_frame,
    size_t event_frame_size)
{
    const robusto_proxy_frame_header_t *header;
    robusto_proxy_pubsub_delivery_t delivery;
    robusto_proxy_pubsub_client_subscription_fields_t *fields;
    uint32_t next_sequence;
    rob_ret_val_t callback_result;

    if (client == NULL || event_frame == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }
    if (robusto_proxy_frame_validate_buffer(event_frame, event_frame_size, NULL) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    header = (const robusto_proxy_frame_header_t *)event_frame;
    if (event_frame_size != robusto_proxy_frame_size_bytes(header->payload_length) ||
        header->flags != ROBUSTO_PROXY_FLAG_EVENT ||
        header->domain != ROBUSTO_PROXY_DOMAIN_PUBSUB ||
        header->opcode != ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY ||
        header->correlation_id != 0U ||
        robusto_proxy_pubsub_decode_delivery(
            event_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
            header->payload_length, &delivery) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    fields = find_subscription_by_id(client, delivery.subscription_id);
    if (fields == NULL)
    {
        client->pubsub_unknown_deliveries += 1U;
        return ROB_ERR_INVALID_ID;
    }
    if (delivery.delivery_sequence != fields->next_delivery_sequence)
    {
        client->pubsub_delivery_sequence_gaps += 1U;
    }
    next_sequence = delivery.delivery_sequence + 1U;
    fields->next_delivery_sequence = next_sequence == 0U ? 1U : next_sequence;
    callback_result = fields->callback(fields->callback_context,
                                       (uint8_t *)delivery.data,
                                       delivery.data_length);
    client->pubsub_delivery_events += 1U;
    return callback_result;
}

void robusto_proxy_pubsub_session_reset(robusto_proxy_client_t *client)
{
    robusto_proxy_pubsub_client_subscription_t *subscriptions;
    uint16_t index;

    if (client == NULL || client->pubsub_subscriptions == NULL)
    {
        return;
    }
    subscriptions = client->pubsub_subscriptions;
    for (index = 0U; index < client->pubsub_subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_client_subscription_fields_t *fields =
            subscription_fields(&subscriptions[index]);
        if (fields->allocated)
        {
            fields->active = false;
            fields->subscription_id = 0U;
            fields->topic_hash = 0U;
            fields->next_delivery_sequence = 1U;
        }
    }
}

rob_ret_val_t robusto_proxy_pubsub_reconcile(robusto_proxy_client_t *client)
{
    robusto_proxy_pubsub_client_subscription_t *subscriptions;
    uint16_t index;

    if (client == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!robusto_proxy_pubsub_is_ready(client))
    {
        return ROB_ERR_NOT_READY;
    }
    if (client->pubsub_subscriptions == NULL)
    {
        return ROB_OK;
    }
    subscriptions = client->pubsub_subscriptions;
    for (index = 0U; index < client->pubsub_subscription_capacity; ++index)
    {
        robusto_proxy_pubsub_client_subscription_fields_t *fields =
            subscription_fields(&subscriptions[index]);
        robusto_proxy_pubsub_subscribe_request_t request;
        robusto_proxy_pubsub_subscribe_response_t response;
        const uint8_t *response_payload;
        size_t response_payload_size;
        size_t request_size;
        rob_ret_val_t result;

        if (!fields->allocated || fields->active)
        {
            continue;
        }
        memset(&request, 0, sizeof(request));
        request.operation_id = robusto_proxy_client_take_operation_id(client);
        request.topic = (const uint8_t *)fields->topic;
        request.topic_length = fields->topic_length;
        request.options = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES;
        if (robusto_proxy_pubsub_encode_subscribe_request(
                client->response_frame, client->response_frame_size,
                &request, &request_size) != ROBUSTO_PROXY_RESULT_OK)
        {
            return ROB_FAIL;
        }
        result = robusto_proxy_client_request(
            client, ROBUSTO_PROXY_DOMAIN_PUBSUB,
            ROBUSTO_PROXY_PUBSUB_OPCODE_SUBSCRIBE,
            client->response_frame, request_size, true,
            &response_payload, &response_payload_size);
        if (result != ROB_OK)
        {
            return result;
        }
        if (robusto_proxy_pubsub_decode_subscribe_response(
                response_payload, response_payload_size, &response) !=
                ROBUSTO_PROXY_RESULT_OK ||
            response.subscription_id == 0U)
        {
            return ROB_ERR_PARSING_FAILED;
        }
        fields->subscription_id = response.subscription_id;
        fields->topic_hash = response.topic_hash;
        fields->next_delivery_sequence = 1U;
        fields->active = true;
    }
    return ROB_OK;
}