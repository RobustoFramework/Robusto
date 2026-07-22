#include "robusto_proxy_pubsub.h"

#include <string.h>

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
    write_le32(bytes, (uint32_t)value);
    write_le32(bytes + 4U, (uint32_t)(value >> 32U));
}

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint64_t read_le64(const uint8_t *bytes)
{
    return (uint64_t)read_le32(bytes) | ((uint64_t)read_le32(bytes + 4U) << 32U);
}

static bool continuation(uint8_t byte)
{
    return (byte & 0xC0U) == 0x80U;
}

bool robusto_proxy_pubsub_topic_is_valid(const uint8_t *topic, uint16_t length)
{
    size_t index = 0U;

    if (topic == NULL || length == 0U || length > ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES)
    {
        return false;
    }

    while (index < length)
    {
        uint8_t first = topic[index];
        if (first <= 0x7FU)
        {
            if (first <= 0x1FU || first == 0x7FU)
            {
                return false;
            }
            ++index;
        }
        else if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= length || !continuation(topic[index + 1U]))
            {
                return false;
            }
            index += 2U;
        }
        else if (first >= 0xE0U && first <= 0xEFU)
        {
            uint8_t second;
            if (index + 2U >= length)
            {
                return false;
            }
            second = topic[index + 1U];
            if (!continuation(second) || !continuation(topic[index + 2U]) ||
                (first == 0xE0U && second < 0xA0U) ||
                (first == 0xEDU && second >= 0xA0U))
            {
                return false;
            }
            index += 3U;
        }
        else if (first >= 0xF0U && first <= 0xF4U)
        {
            uint8_t second;
            if (index + 3U >= length)
            {
                return false;
            }
            second = topic[index + 1U];
            if (!continuation(second) || !continuation(topic[index + 2U]) ||
                !continuation(topic[index + 3U]) ||
                (first == 0xF0U && second < 0x90U) ||
                (first == 0xF4U && second > 0x8FU))
            {
                return false;
            }
            index += 4U;
        }
        else
        {
            return false;
        }
    }
    return true;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_request_t *request, size_t *encoded_size)
{
    size_t required_size;
    if (buffer == NULL || request == NULL || encoded_size == NULL ||
        request->operation_id == 0U || !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length) ||
        request->data_length > ROBUSTO_PROXY_PUBSUB_MAX_INLINE_DATA_BYTES ||
        (request->data_length > 0U && request->data == NULL))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_HEADER_SIZE_BYTES +
                    request->topic_length + request->data_length;
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    write_le16(buffer + 8U, request->topic_length);
    write_le16(buffer + 10U, 0U);
    write_le32(buffer + 12U, request->data_length);
    memcpy(buffer + 16U, request->topic, request->topic_length);
    if (request->data_length > 0U)
    {
        memcpy(buffer + 16U + request->topic_length, request->data, request->data_length);
    }
    *encoded_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_request_t *request)
{
    size_t required_size;
    if (buffer == NULL || request == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_PUBLISH_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    request->topic_length = read_le16(buffer + 8U);
    request->data_length = read_le32(buffer + 12U);
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_HEADER_SIZE_BYTES +
                    request->topic_length + request->data_length;
    if (required_size != buffer_size ||
        request->data_length > ROBUSTO_PROXY_PUBSUB_MAX_INLINE_DATA_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (read_le16(buffer + 10U) != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    request->topic = buffer + 16U;
    request->data = request->topic + request->topic_length;
    if (request->operation_id == 0U || !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_begin_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_begin_request_t *request, size_t *encoded_size)
{
    size_t required_size;

    if (buffer == NULL || request == NULL || encoded_size == NULL ||
        request->operation_id == 0U || request->data_length == 0U ||
        !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES +
                    request->topic_length;
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    write_le16(buffer + 8U, request->topic_length);
    write_le16(buffer + 10U, 0U);
    write_le32(buffer + 12U, request->data_length);
    memcpy(buffer + ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES,
           request->topic, request->topic_length);
    *encoded_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_begin_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_begin_request_t *request)
{
    size_t required_size;

    if (buffer == NULL || request == NULL ||
        buffer_size < ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    request->topic_length = read_le16(buffer + 8U);
    request->data_length = read_le32(buffer + 12U);
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES +
                    request->topic_length;
    if (required_size != buffer_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (read_le16(buffer + 10U) != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    request->topic = buffer + ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES;
    if (request->operation_id == 0U || request->data_length == 0U ||
        !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_chunk_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_chunk_request_t *request, size_t *encoded_size)
{
    size_t required_size;

    if (buffer == NULL || request == NULL || encoded_size == NULL ||
        request->operation_id == 0U || request->data == NULL ||
        request->data_length == 0U ||
        request->data_length > ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES +
                    request->data_length;
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    write_le32(buffer + 8U, request->offset);
    write_le32(buffer + 12U, request->data_length);
    memcpy(buffer + ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES,
           request->data, request->data_length);
    *encoded_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_chunk_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_chunk_request_t *request)
{
    size_t required_size;

    if (buffer == NULL || request == NULL ||
        buffer_size < ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    request->offset = read_le32(buffer + 8U);
    request->data_length = read_le32(buffer + 12U);
    required_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES +
                    request->data_length;
    if (required_size != buffer_size || request->data_length == 0U ||
        request->data_length > ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->data = buffer + ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES;
    return request->operation_id != 0U ? ROBUSTO_PROXY_RESULT_OK
                                       : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_transfer_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_transfer_request_t *request)
{
    if (buffer == NULL || request == NULL || request->operation_id == 0U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    if (buffer_size < ROBUSTO_PROXY_PUBSUB_PUBLISH_TRANSFER_REQUEST_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_transfer_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_transfer_request_t *request)
{
    if (buffer == NULL || request == NULL ||
        buffer_size != ROBUSTO_PROXY_PUBSUB_PUBLISH_TRANSFER_REQUEST_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    return request->operation_id != 0U ? ROBUSTO_PROXY_RESULT_OK
                                       : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_subscribe_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_subscribe_request_t *request, size_t *encoded_size)
{
    size_t required_size;
    if (buffer == NULL || request == NULL || encoded_size == NULL || request->operation_id == 0U ||
        request->options != ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES ||
        !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    required_size = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_HEADER_SIZE_BYTES + request->topic_length;
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    write_le16(buffer + 8U, request->topic_length);
    buffer[10] = request->options;
    buffer[11] = 0U;
    memcpy(buffer + 12U, request->topic, request->topic_length);
    *encoded_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_subscribe_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_subscribe_request_t *request)
{
    size_t required_size;
    if (buffer == NULL || request == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    request->topic_length = read_le16(buffer + 8U);
    request->options = buffer[10];
    required_size = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_HEADER_SIZE_BYTES + request->topic_length;
    if (required_size != buffer_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (buffer[11] != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    request->topic = buffer + 12U;
    if (request->operation_id == 0U ||
        request->options != ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES ||
        !robusto_proxy_pubsub_topic_is_valid(request->topic, request->topic_length))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_unsubscribe_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_unsubscribe_request_t *request)
{
    if (buffer == NULL || request == NULL || request->operation_id == 0U ||
        request->subscription_id == 0U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    if (buffer_size < ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_REQUEST_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le64(buffer, request->operation_id);
    write_le32(buffer + 8U, request->subscription_id);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_unsubscribe_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_unsubscribe_request_t *request)
{
    if (buffer == NULL || request == NULL || buffer_size != ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_REQUEST_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    request->operation_id = read_le64(buffer);
    request->subscription_id = read_le32(buffer + 8U);
    return request->operation_id != 0U && request->subscription_id != 0U
               ? ROBUSTO_PROXY_RESULT_OK
               : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_PUBLISH_RESPONSE_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    write_le32(buffer, response->topic_hash);
    write_le32(buffer + 4U, response->delivery_count);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size != ROBUSTO_PROXY_PUBSUB_PUBLISH_RESPONSE_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    response->topic_hash = read_le32(buffer);
    response->delivery_count = read_le32(buffer + 4U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_subscribe_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_subscribe_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_RESPONSE_SIZE_BYTES ||
        response->subscription_id == 0U || response->created > 1U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    write_le32(buffer, response->subscription_id);
    write_le32(buffer + 4U, response->topic_hash);
    buffer[8] = response->created;
    memset(buffer + 9U, 0, 3U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_subscribe_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_subscribe_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size != ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_RESPONSE_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (buffer[9] != 0U || buffer[10] != 0U || buffer[11] != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    response->subscription_id = read_le32(buffer);
    response->topic_hash = read_le32(buffer + 4U);
    response->created = buffer[8];
    return response->subscription_id != 0U && response->created <= 1U
               ? ROBUSTO_PROXY_RESULT_OK
               : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_unsubscribe_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_unsubscribe_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_RESPONSE_SIZE_BYTES ||
        response->removed > 1U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    buffer[0] = response->removed;
    memset(buffer + 1U, 0, 3U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_unsubscribe_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_unsubscribe_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size != ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_RESPONSE_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (buffer[1] != 0U || buffer[2] != 0U || buffer[3] != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    response->removed = buffer[0];
    return response->removed <= 1U ? ROBUSTO_PROXY_RESULT_OK : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_status_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_status_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_STATUS_RESPONSE_SIZE_BYTES ||
        response->state > 3U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    memset(buffer, 0, ROBUSTO_PROXY_PUBSUB_STATUS_RESPONSE_SIZE_BYTES);
    buffer[0] = response->state;
    write_le32(buffer + 4U, response->active_subscriptions);
    write_le32(buffer + 8U, response->publish_requests);
    write_le32(buffer + 12U, response->subscribe_requests);
    write_le32(buffer + 16U, response->unsubscribe_requests);
    write_le32(buffer + 20U, response->delivery_events);
    write_le32(buffer + 24U, response->delivery_drops);
    write_le32(buffer + 28U, response->duplicate_operations);
    write_le32(buffer + 32U, response->pubsub_errors);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_status_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_status_response_t *response)
{
    if (buffer == NULL || response == NULL || buffer_size != ROBUSTO_PROXY_PUBSUB_STATUS_RESPONSE_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    if (buffer[1] != 0U || buffer[2] != 0U || buffer[3] != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }
    response->state = buffer[0];
    response->active_subscriptions = read_le32(buffer + 4U);
    response->publish_requests = read_le32(buffer + 8U);
    response->subscribe_requests = read_le32(buffer + 12U);
    response->unsubscribe_requests = read_le32(buffer + 16U);
    response->delivery_events = read_le32(buffer + 20U);
    response->delivery_drops = read_le32(buffer + 24U);
    response->duplicate_operations = read_le32(buffer + 28U);
    response->pubsub_errors = read_le32(buffer + 32U);
    return response->state <= 3U ? ROBUSTO_PROXY_RESULT_OK : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}

robusto_proxy_result_t robusto_proxy_pubsub_encode_delivery(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_delivery_t *delivery, size_t *encoded_size)
{
    size_t required_size;
    if (buffer == NULL || delivery == NULL || encoded_size == NULL ||
        delivery->subscription_id == 0U || delivery->delivery_sequence == 0U ||
        delivery->data_length > ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES ||
        (delivery->data_length > 0U && delivery->data == NULL))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    required_size = ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES + delivery->data_length;
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    write_le32(buffer, delivery->subscription_id);
    write_le32(buffer + 4U, delivery->delivery_sequence);
    write_le32(buffer + 8U, delivery->data_length);
    if (delivery->data_length > 0U)
    {
        memcpy(buffer + 12U, delivery->data, delivery->data_length);
    }
    *encoded_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_pubsub_decode_delivery(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_delivery_t *delivery)
{
    size_t required_size;
    if (buffer == NULL || delivery == NULL || buffer_size < ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    delivery->subscription_id = read_le32(buffer);
    delivery->delivery_sequence = read_le32(buffer + 4U);
    delivery->data_length = read_le32(buffer + 8U);
    required_size = ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES + delivery->data_length;
    if (required_size != buffer_size ||
        delivery->data_length > ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }
    delivery->data = buffer + 12U;
    return delivery->subscription_id != 0U && delivery->delivery_sequence != 0U
               ? ROBUSTO_PROXY_RESULT_OK
               : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
}