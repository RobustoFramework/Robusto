#include "../include/robusto_rsd1_protocol.h"

#include <string.h>

#include "robusto_proxy_crc32.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0]) | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

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

robusto_rsd1_result_t robusto_rsd1_encode(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t message_id,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    size_t *packet_size)
{
    size_t required_size;
    uint32_t crc;

    if (buffer == NULL || packet_size == NULL || message_id == 0U ||
        sequence == 0U || (payload_size > 0U && payload == NULL)) {
        return ROBUSTO_RSD1_INVALID_ARGUMENT;
    }
    if (payload_size > ROBUSTO_RSD1_MAX_PAYLOAD_SIZE) {
        return ROBUSTO_RSD1_BAD_LENGTH;
    }

    required_size = ROBUSTO_RSD1_HEADER_SIZE + payload_size +
                    ROBUSTO_RSD1_CRC_SIZE;
    if (buffer_size < required_size) {
        return ROBUSTO_RSD1_BAD_LENGTH;
    }

    memset(buffer, 0, required_size);
    buffer[0] = 'R';
    buffer[1] = 'S';
    buffer[2] = 'D';
    buffer[3] = '1';
    buffer[4] = ROBUSTO_RSD1_VERSION;
    buffer[5] = ROBUSTO_RSD1_HEADER_SIZE;
    write_le32(buffer + 8U, message_id);
    write_le32(buffer + 12U, sequence);
    write_le16(buffer + 16U, payload_size);
    if (payload_size > 0U) {
        memcpy(buffer + ROBUSTO_RSD1_HEADER_SIZE, payload, payload_size);
    }

    crc = robusto_proxy_crc32_iso_hdlc(
        buffer, required_size - ROBUSTO_RSD1_CRC_SIZE);
    write_le32(buffer + required_size - ROBUSTO_RSD1_CRC_SIZE, crc);
    *packet_size = required_size;
    return ROBUSTO_RSD1_OK;
}

robusto_rsd1_result_t robusto_rsd1_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_rsd1_packet_view_t *packet)
{
    uint16_t payload_size;
    size_t required_size;
    uint32_t expected_crc;
    uint32_t actual_crc;

    if (buffer == NULL || packet == NULL) {
        return ROBUSTO_RSD1_INVALID_ARGUMENT;
    }
    if (buffer_size < ROBUSTO_RSD1_HEADER_SIZE + ROBUSTO_RSD1_CRC_SIZE) {
        return ROBUSTO_RSD1_BAD_LENGTH;
    }
    if (buffer[0] != 'R' || buffer[1] != 'S' ||
        buffer[2] != 'D' || buffer[3] != '1') {
        return ROBUSTO_RSD1_BAD_MAGIC;
    }
    if (buffer[4] != ROBUSTO_RSD1_VERSION ||
        buffer[5] != ROBUSTO_RSD1_HEADER_SIZE) {
        return ROBUSTO_RSD1_BAD_VERSION;
    }
    if (buffer[6] != 0U || buffer[7] != 0U ||
        buffer[18] != 0U || buffer[19] != 0U) {
        return ROBUSTO_RSD1_BAD_RESERVED;
    }

    payload_size = read_le16(buffer + 16U);
    if (payload_size > ROBUSTO_RSD1_MAX_PAYLOAD_SIZE) {
        return ROBUSTO_RSD1_BAD_LENGTH;
    }
    required_size = ROBUSTO_RSD1_HEADER_SIZE + payload_size +
                    ROBUSTO_RSD1_CRC_SIZE;
    if (buffer_size != required_size) {
        return ROBUSTO_RSD1_BAD_LENGTH;
    }

    expected_crc = robusto_proxy_crc32_iso_hdlc(
        buffer, required_size - ROBUSTO_RSD1_CRC_SIZE);
    actual_crc = read_le32(buffer + required_size - ROBUSTO_RSD1_CRC_SIZE);
    if (expected_crc != actual_crc) {
        return ROBUSTO_RSD1_BAD_CRC;
    }

    packet->message_id = read_le32(buffer + 8U);
    packet->sequence = read_le32(buffer + 12U);
    packet->payload = buffer + ROBUSTO_RSD1_HEADER_SIZE;
    packet->payload_size = payload_size;
    if (packet->message_id == 0U || packet->sequence == 0U) {
        return ROBUSTO_RSD1_INVALID_ARGUMENT;
    }
    return ROBUSTO_RSD1_OK;
}