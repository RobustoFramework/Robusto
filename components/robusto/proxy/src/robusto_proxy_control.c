#include "robusto_proxy_control.h"

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
    write_le32(bytes, (uint32_t)(value & 0xFFFFFFFFULL));
    write_le32(bytes + 4U, (uint32_t)((value >> 32U) & 0xFFFFFFFFULL));
}

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0]) |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint64_t read_le64(const uint8_t *bytes)
{
    return ((uint64_t)read_le32(bytes)) |
           ((uint64_t)read_le32(bytes + 4U) << 32U);
}

static robusto_proxy_result_t check_output(void *output, size_t buffer_size, size_t required_size)
{
    if (output == NULL || buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    return ROBUSTO_PROXY_RESULT_OK;
}

static robusto_proxy_result_t check_io(const uint8_t *input, void *output, size_t buffer_size, size_t required_size)
{
    if ((input == NULL && output == NULL) || buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    return ROBUSTO_PROXY_RESULT_OK;
}

static robusto_proxy_result_t decode_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    void *response,
    size_t response_size,
    robusto_proxy_result_t (*decode_payload)(const uint8_t *, size_t, void *))
{
    size_t prefix_and_detail_size;

    if (buffer == NULL || prefix == NULL || response == NULL || decode_payload == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    if (robusto_proxy_decode_response_prefix(buffer, buffer_size, prefix) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    prefix_and_detail_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + prefix->detail_length;
    if (prefix_and_detail_size > buffer_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    memset(response, 0, response_size);
    if (!robusto_proxy_response_prefix_is_success(prefix))
    {
        return ROBUSTO_PROXY_RESULT_OK;
    }

    return decode_payload(
        buffer + prefix_and_detail_size,
        buffer_size - prefix_and_detail_size,
        response);
}

robusto_proxy_result_t robusto_proxy_encode_response_prefix(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_response_prefix_t *prefix)
{
    if (check_output(buffer, buffer_size, ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || prefix == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    memset(buffer, 0, ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES);
    write_le16(buffer + 0U, prefix->status);
    write_le16(buffer + 2U, prefix->result_flags);
    write_le32(buffer + 4U, prefix->retry_after_ms);
    write_le16(buffer + 8U, prefix->detail_length);
    write_le16(buffer + 10U, prefix->reserved);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_response_prefix(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix)
{
    if (check_io(buffer, prefix, buffer_size, ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || prefix == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    prefix->status = read_le16(buffer + 0U);
    prefix->result_flags = read_le16(buffer + 2U);
    prefix->retry_after_ms = read_le32(buffer + 4U);
    prefix->detail_length = read_le16(buffer + 8U);
    prefix->reserved = read_le16(buffer + 10U);

    if ((prefix->result_flags & ~ROBUSTO_PROXY_RESULT_FLAG_RETRYABLE) != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }

    if (prefix->detail_length > 128U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    if (prefix->reserved != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }

    if (buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + prefix->detail_length)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    return ROBUSTO_PROXY_RESULT_OK;
}

bool robusto_proxy_response_prefix_is_success(const robusto_proxy_response_prefix_t *prefix)
{
    return prefix != NULL && prefix->status == ROBUSTO_PROXY_STATUS_OK;
}

robusto_proxy_result_t robusto_proxy_encode_hello_request(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_hello_request_t *request)
{
    if (check_io(buffer, (void *)request, buffer_size, ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || request == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    memset(buffer, 0, ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES);
    write_le64(buffer + 0U, request->controller_boot_id);
    buffer[8] = request->min_protocol_major;
    buffer[9] = request->max_protocol_major;
    buffer[10] = request->min_protocol_minor;
    buffer[11] = request->max_protocol_minor;
    write_le32(buffer + 12U, request->max_payload);
    write_le16(buffer + 16U, request->max_in_flight);
    write_le16(buffer + 18U, request->reserved);
    write_le64(buffer + 20U, request->required_features);
    write_le64(buffer + 28U, request->optional_features);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_hello_request(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_hello_request_t *request)
{
    if (check_io(buffer, request, buffer_size, ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || request == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    request->controller_boot_id = read_le64(buffer + 0U);
    request->min_protocol_major = buffer[8];
    request->max_protocol_major = buffer[9];
    request->min_protocol_minor = buffer[10];
    request->max_protocol_minor = buffer[11];
    request->max_payload = read_le32(buffer + 12U);
    request->max_in_flight = read_le16(buffer + 16U);
    request->reserved = read_le16(buffer + 18U);
    request->required_features = read_le64(buffer + 20U);
    request->optional_features = read_le64(buffer + 28U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_encode_hello_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_hello_response_t *response)
{
    if (check_io(buffer, (void *)response, buffer_size, ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    memset(buffer, 0, ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES);
    write_le64(buffer + 0U, response->proxy_boot_id);
    buffer[8] = response->selected_protocol_major;
    buffer[9] = response->selected_protocol_minor;
    write_le16(buffer + 10U, response->reserved);
    write_le32(buffer + 12U, response->selected_max_payload);
    write_le16(buffer + 16U, response->selected_max_in_flight);
    write_le16(buffer + 18U, response->dedupe_entries);
    write_le32(buffer + 20U, response->dedupe_window_ms);
    write_le64(buffer + 24U, response->enabled_features);
    write_le32(buffer + 32U, response->proxy_uptime_ms);
    write_le32(buffer + 36U, response->reserved2);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_hello_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_hello_response_t *response)
{
    if (check_io(buffer, response, buffer_size, ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    response->proxy_boot_id = read_le64(buffer + 0U);
    response->selected_protocol_major = buffer[8];
    response->selected_protocol_minor = buffer[9];
    response->reserved = read_le16(buffer + 10U);
    response->selected_max_payload = read_le32(buffer + 12U);
    response->selected_max_in_flight = read_le16(buffer + 16U);
    response->dedupe_entries = read_le16(buffer + 18U);
    response->dedupe_window_ms = read_le32(buffer + 20U);
    response->enabled_features = read_le64(buffer + 24U);
    response->proxy_uptime_ms = read_le32(buffer + 32U);
    response->reserved2 = read_le32(buffer + 36U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_encode_capability_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_capability_response_t *response)
{
    if (check_io(buffer, (void *)response, buffer_size, ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    memset(buffer, 0, ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES);
    write_le64(buffer + 0U, response->proxy_boot_id);
    write_le64(buffer + 8U, response->enabled_features);
    buffer[16] = response->pubsub_major;
    buffer[17] = response->pubsub_minor;
    write_le16(buffer + 18U, response->reserved);
    write_le32(buffer + 20U, response->selected_max_payload);
    write_le16(buffer + 24U, response->selected_max_in_flight);
    write_le16(buffer + 26U, response->max_subscriptions);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_capability_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_capability_response_t *response)
{
    if (check_io(buffer, response, buffer_size, ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    response->proxy_boot_id = read_le64(buffer + 0U);
    response->enabled_features = read_le64(buffer + 8U);
    response->pubsub_major = buffer[16];
    response->pubsub_minor = buffer[17];
    response->reserved = read_le16(buffer + 18U);
    response->selected_max_payload = read_le32(buffer + 20U);
    response->selected_max_in_flight = read_le16(buffer + 24U);
    response->max_subscriptions = read_le16(buffer + 26U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_encode_health_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_health_response_t *response)
{
    if (check_io(buffer, (void *)response, buffer_size, ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    memset(buffer, 0, ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES);
    write_le64(buffer + 0U, response->proxy_boot_id);
    write_le32(buffer + 8U, response->uptime_ms);
    buffer[12] = response->service_state;
    memcpy(buffer + 13U, response->reserved, sizeof(response->reserved));
    write_le32(buffer + 16U, response->requests);
    write_le32(buffer + 20U, response->events);
    write_le32(buffer + 24U, response->errors);
    write_le32(buffer + 28U, response->timeouts);
    write_le32(buffer + 32U, response->rx_crc_errors);
    write_le32(buffer + 36U, response->queue_high_water);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_health_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_health_response_t *response)
{
    if (check_io(buffer, response, buffer_size, ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES) != ROBUSTO_PROXY_RESULT_OK || response == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    response->proxy_boot_id = read_le64(buffer + 0U);
    response->uptime_ms = read_le32(buffer + 8U);
    response->service_state = buffer[12];
    memcpy(response->reserved, buffer + 13U, sizeof(response->reserved));
    response->requests = read_le32(buffer + 16U);
    response->events = read_le32(buffer + 20U);
    response->errors = read_le32(buffer + 24U);
    response->timeouts = read_le32(buffer + 28U);
    response->rx_crc_errors = read_le32(buffer + 32U);
    response->queue_high_water = read_le32(buffer + 36U);
    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_decode_hello_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_hello_response_t *response)
{
    return decode_response_message(
        buffer,
        buffer_size,
        prefix,
        response,
        sizeof(*response),
        (robusto_proxy_result_t (*)(const uint8_t *, size_t, void *))robusto_proxy_decode_hello_response);
}

robusto_proxy_result_t robusto_proxy_decode_capability_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_capability_response_t *response)
{
    return decode_response_message(
        buffer,
        buffer_size,
        prefix,
        response,
        sizeof(*response),
        (robusto_proxy_result_t (*)(const uint8_t *, size_t, void *))robusto_proxy_decode_capability_response);
}

robusto_proxy_result_t robusto_proxy_decode_health_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_health_response_t *response)
{
    return decode_response_message(
        buffer,
        buffer_size,
        prefix,
        response,
        sizeof(*response),
        (robusto_proxy_result_t (*)(const uint8_t *, size_t, void *))robusto_proxy_decode_health_response);
}
