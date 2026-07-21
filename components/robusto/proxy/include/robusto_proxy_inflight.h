#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS 8U

typedef enum robusto_proxy_inflight_status {
    ROBUSTO_PROXY_INFLIGHT_STATUS_FREE = 0,
    ROBUSTO_PROXY_INFLIGHT_STATUS_ACTIVE,
    ROBUSTO_PROXY_INFLIGHT_STATUS_COMPLETED,
    ROBUSTO_PROXY_INFLIGHT_STATUS_STALE_SESSION,
} robusto_proxy_inflight_status_t;

typedef struct robusto_proxy_inflight_entry {
    robusto_proxy_inflight_status_t status;
    uint8_t domain;
    uint8_t opcode;
    uint32_t correlation_id;
    uint32_t sequence;
    uint32_t started_at_ms;
    uint32_t timeout_ms;
    uint64_t peer_boot_id;
} robusto_proxy_inflight_entry_t;

typedef struct robusto_proxy_inflight_table {
    uint16_t capacity;
    uint16_t active_count;
    robusto_proxy_inflight_entry_t entries[ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS];
} robusto_proxy_inflight_table_t;

void robusto_proxy_inflight_init(
    robusto_proxy_inflight_table_t *table,
    uint16_t negotiated_capacity);

robusto_proxy_result_t robusto_proxy_inflight_begin(
    robusto_proxy_inflight_table_t *table,
    uint8_t domain,
    uint8_t opcode,
    uint32_t correlation_id,
    uint32_t sequence,
    uint32_t started_at_ms,
    uint32_t timeout_ms,
    uint64_t peer_boot_id);

robusto_proxy_inflight_entry_t *robusto_proxy_inflight_find(
    robusto_proxy_inflight_table_t *table,
    uint32_t correlation_id);

bool robusto_proxy_inflight_complete(
    robusto_proxy_inflight_table_t *table,
    uint32_t correlation_id);

bool robusto_proxy_inflight_is_expired(
    const robusto_proxy_inflight_entry_t *entry,
    uint32_t now_ms);

uint16_t robusto_proxy_inflight_expire(
    robusto_proxy_inflight_table_t *table,
    uint32_t now_ms);

uint16_t robusto_proxy_inflight_invalidate_peer(
    robusto_proxy_inflight_table_t *table,
    uint64_t peer_boot_id);

#ifdef __cplusplus
}
#endif
