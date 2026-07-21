#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void robusto_proxy_frame_header_init(
    robusto_proxy_frame_header_t *header,
    uint8_t flags,
    uint8_t domain,
    uint8_t opcode,
    uint32_t correlation_id,
    uint32_t sequence,
    uint32_t payload_length);

robusto_proxy_result_t robusto_proxy_frame_validate_header(
    const robusto_proxy_frame_header_t *header);

robusto_proxy_result_t robusto_proxy_frame_validate_buffer(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_crc);

robusto_proxy_result_t robusto_proxy_frame_encode(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_frame_header_t *header,
    const uint8_t *payload,
    size_t *frame_size);

bool robusto_proxy_slot_is_empty(const uint8_t *slot, size_t length);

#ifdef __cplusplus
}
#endif