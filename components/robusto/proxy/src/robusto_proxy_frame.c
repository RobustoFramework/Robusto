#include "robusto_proxy_frame.h"

#include <string.h>

#include "robusto_proxy_crc32.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

bool robusto_proxy_flag_is_valid(uint8_t flags)
{
    uint8_t frame_type_bits = flags &
                              (ROBUSTO_PROXY_FLAG_REQUEST |
                               ROBUSTO_PROXY_FLAG_RESPONSE |
                               ROBUSTO_PROXY_FLAG_EVENT);

    if ((flags & 0xF0U) != 0U)
    {
        return false;
    }

    return frame_type_bits == ROBUSTO_PROXY_FLAG_REQUEST ||
           frame_type_bits == ROBUSTO_PROXY_FLAG_RESPONSE ||
           frame_type_bits == ROBUSTO_PROXY_FLAG_EVENT;
}

bool robusto_proxy_payload_length_is_valid(uint32_t payload_length)
{
    return payload_length <= ROBUSTO_PROXY_MAX_PAYLOAD_BYTES;
}

size_t robusto_proxy_frame_size_bytes(uint32_t payload_length)
{
    return ROBUSTO_PROXY_HEADER_SIZE_BYTES +
           (size_t)payload_length +
           ROBUSTO_PROXY_CRC_SIZE_BYTES;
}

robusto_proxy_profile_limits_t robusto_proxy_profile_limits(robusto_proxy_profile_t profile)
{
    robusto_proxy_profile_limits_t limits;

    if (profile == ROBUSTO_PROXY_PROFILE_STANDARD)
    {
        limits.max_in_flight = 8U;
        limits.max_subscriptions = 32U;
        limits.dedupe_entries = 64U;
        limits.dedupe_window_ms = 10000U;
        limits.request_pool_bytes = 8192U;
        limits.response_pool_bytes = 8192U;
        return limits;
    }

    limits.max_in_flight = 2U;
    limits.max_subscriptions = 16U;
    limits.dedupe_entries = 32U;
    limits.dedupe_window_ms = 10000U;
    limits.request_pool_bytes = 4096U;
    limits.response_pool_bytes = 4096U;
    return limits;
}

void robusto_proxy_frame_header_init(
    robusto_proxy_frame_header_t *header,
    uint8_t flags,
    uint8_t domain,
    uint8_t opcode,
    uint32_t correlation_id,
    uint32_t sequence,
    uint32_t payload_length)
{
    header->magic[0] = 0x52U;
    header->magic[1] = 0x50U;
    header->protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    header->header_length = ROBUSTO_PROXY_HEADER_SIZE_BYTES;
    header->flags = flags;
    header->domain = domain;
    header->opcode = opcode;
    header->reserved = 0U;
    header->correlation_id = correlation_id;
    header->sequence = sequence;
    header->payload_length = payload_length;
}

robusto_proxy_result_t robusto_proxy_frame_validate_header(
    const robusto_proxy_frame_header_t *header)
{
    if (header == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    if (header->magic[0] != 0x52U || header->magic[1] != 0x50U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_MAGIC;
    }

    if (header->protocol_major != ROBUSTO_PROXY_PROTOCOL_MAJOR)
    {
        return ROBUSTO_PROXY_RESULT_BAD_VERSION;
    }

    if (header->header_length != ROBUSTO_PROXY_HEADER_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_HEADER_LENGTH;
    }

    if (!robusto_proxy_flag_is_valid(header->flags))
    {
        return ROBUSTO_PROXY_RESULT_BAD_FLAGS;
    }

    if (header->reserved != 0U)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }

    if (!robusto_proxy_payload_length_is_valid(header->payload_length))
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    if (robusto_proxy_frame_size_bytes(header->payload_length) > ROBUSTO_PROXY_SLOT_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_frame_validate_buffer(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_crc)
{
    const robusto_proxy_frame_header_t *header;
    size_t frame_size;
    uint32_t expected_crc;
    uint32_t actual_crc;
    robusto_proxy_result_t result;

    if (buffer == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    if (length < ROBUSTO_PROXY_HEADER_SIZE_BYTES + ROBUSTO_PROXY_CRC_SIZE_BYTES)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    header = (const robusto_proxy_frame_header_t *)buffer;
    result = robusto_proxy_frame_validate_header(header);
    if (result != ROBUSTO_PROXY_RESULT_OK)
    {
        return result;
    }

    frame_size = robusto_proxy_frame_size_bytes(header->payload_length);
    if (length < frame_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    expected_crc = robusto_proxy_crc32_iso_hdlc(buffer, frame_size - ROBUSTO_PROXY_CRC_SIZE_BYTES);
    actual_crc = read_le32(buffer + frame_size - ROBUSTO_PROXY_CRC_SIZE_BYTES);
    if (out_crc != NULL)
    {
        *out_crc = expected_crc;
    }

    if (expected_crc != actual_crc)
    {
        return ROBUSTO_PROXY_RESULT_BAD_CRC;
    }

    return ROBUSTO_PROXY_RESULT_OK;
}

robusto_proxy_result_t robusto_proxy_frame_encode(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_frame_header_t *header,
    const uint8_t *payload,
    size_t *frame_size)
{
    size_t required_size;
    uint32_t crc;
    robusto_proxy_result_t result;

    if (buffer == NULL || header == NULL || frame_size == NULL ||
        (header->payload_length > 0U && payload == NULL))
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    result = robusto_proxy_frame_validate_header(header);
    if (result != ROBUSTO_PROXY_RESULT_OK)
    {
        return result;
    }

    required_size = robusto_proxy_frame_size_bytes(header->payload_length);
    if (buffer_size < required_size)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    memset(buffer, 0, required_size);
    buffer[0] = header->magic[0];
    buffer[1] = header->magic[1];
    buffer[2] = header->protocol_major;
    buffer[3] = header->header_length;
    buffer[4] = header->flags;
    buffer[5] = header->domain;
    buffer[6] = header->opcode;
    buffer[7] = header->reserved;
    write_le32(buffer + 8U, header->correlation_id);
    write_le32(buffer + 12U, header->sequence);
    write_le32(buffer + 16U, header->payload_length);
    if (header->payload_length > 0U)
    {
        memcpy(buffer + ROBUSTO_PROXY_HEADER_SIZE_BYTES, payload, header->payload_length);
    }

    crc = robusto_proxy_crc32_iso_hdlc(buffer, required_size - ROBUSTO_PROXY_CRC_SIZE_BYTES);
    write_le32(buffer + required_size - ROBUSTO_PROXY_CRC_SIZE_BYTES, crc);
    *frame_size = required_size;
    return ROBUSTO_PROXY_RESULT_OK;
}

bool robusto_proxy_slot_is_empty(const uint8_t *slot, size_t length)
{
    if (slot == NULL || length < 2U)
    {
        return false;
    }

    return slot[0] == 0U && slot[1] == 0U;
}