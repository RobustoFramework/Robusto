#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_RSD1_VERSION 1U
#define ROBUSTO_RSD1_HEADER_SIZE 20U
#define ROBUSTO_RSD1_CRC_SIZE 4U
#define ROBUSTO_RSD1_MAX_PAYLOAD_SIZE 4120U
#define ROBUSTO_RSD1_MAX_PACKET_SIZE \
    (ROBUSTO_RSD1_HEADER_SIZE + ROBUSTO_RSD1_MAX_PAYLOAD_SIZE + \
     ROBUSTO_RSD1_CRC_SIZE)

typedef struct robusto_rsd1_packet_view {
    uint32_t message_id;
    uint32_t sequence;
    const uint8_t *payload;
    uint16_t payload_size;
} robusto_rsd1_packet_view_t;

typedef enum robusto_rsd1_result {
    ROBUSTO_RSD1_OK = 0,
    ROBUSTO_RSD1_INVALID_ARGUMENT,
    ROBUSTO_RSD1_BAD_MAGIC,
    ROBUSTO_RSD1_BAD_VERSION,
    ROBUSTO_RSD1_BAD_RESERVED,
    ROBUSTO_RSD1_BAD_LENGTH,
    ROBUSTO_RSD1_BAD_CRC,
} robusto_rsd1_result_t;

robusto_rsd1_result_t robusto_rsd1_encode(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t message_id,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    size_t *packet_size);

robusto_rsd1_result_t robusto_rsd1_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_rsd1_packet_view_t *packet);

robusto_rsd1_result_t robusto_rsd1_decode_prefix(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_rsd1_packet_view_t *packet,
    size_t *packet_size);

#ifdef __cplusplus
}
#endif