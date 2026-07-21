#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robusto_proxy_session {
    robusto_proxy_session_state_t state;
    robusto_proxy_profile_t local_profile;
    robusto_proxy_profile_limits_t local_limits;
    robusto_proxy_profile_limits_t negotiated_limits;
    uint64_t local_boot_id;
    uint64_t peer_boot_id;
    uint64_t enabled_features;
    uint8_t selected_protocol_major;
    uint8_t selected_protocol_minor;
    uint32_t timeout_events;
    uint32_t next_correlation_id;
    uint32_t next_sequence;
} robusto_proxy_session_t;

void robusto_proxy_session_init(
    robusto_proxy_session_t *session,
    robusto_proxy_profile_t profile,
    uint64_t local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed);

void robusto_proxy_session_reset(
    robusto_proxy_session_t *session,
    uint64_t new_local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed);

uint32_t robusto_proxy_session_take_correlation_id(robusto_proxy_session_t *session);
uint32_t robusto_proxy_session_take_sequence(robusto_proxy_session_t *session);
bool robusto_proxy_session_apply_hello_response(
    robusto_proxy_session_t *session,
    const robusto_proxy_hello_response_t *response);

bool robusto_proxy_session_note_expired_requests(
    robusto_proxy_session_t *session,
    uint16_t expired_count);

bool robusto_proxy_session_apply_health_metrics(
    const robusto_proxy_session_t *session,
    robusto_proxy_health_response_t *response);

#ifdef __cplusplus
}
#endif
