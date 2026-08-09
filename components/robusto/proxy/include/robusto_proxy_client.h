#pragma once

#include <stddef.h>
#include <stdint.h>

#include <robusto_retval.h>

#include "robusto_proxy_inflight.h"
#include "robusto_proxy_session.h"
#include "robusto_proxy_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*robusto_proxy_clock_now_ms)(void *context);
typedef void (*robusto_proxy_clock_wait_ms)(void *context, uint32_t delay_ms);
typedef uint32_t (*robusto_proxy_retry_jitter_ms)(void *context, uint32_t maximum_ms);

/**
 * Configuration for robusto_proxy_client_init.
 *
 * request_frame/response_frame must remain valid for the client lifetime.
 */
typedef struct robusto_proxy_client_config {
    robusto_proxy_profile_t profile;
    uint64_t controller_boot_id;
    uint32_t correlation_seed;
    uint32_t sequence_seed;
    uint64_t operation_seed;
    uint32_t request_timeout_ms;
    robusto_proxy_transport_exchange exchange;
    void *transport_context;
    robusto_proxy_clock_now_ms now_ms;
    robusto_proxy_clock_wait_ms wait_ms;
    robusto_proxy_retry_jitter_ms retry_jitter_ms;
    void *clock_context;
    uint8_t *request_frame;
    size_t request_frame_size;
    uint8_t *response_frame;
    size_t response_frame_size;
} robusto_proxy_client_config_t;

typedef struct robusto_proxy_client {
    robusto_proxy_session_t session;
    robusto_proxy_inflight_table_t inflight;
    robusto_proxy_transport_exchange exchange;
    void *transport_context;
    robusto_proxy_clock_now_ms now_ms;
    robusto_proxy_clock_wait_ms wait_ms;
    robusto_proxy_retry_jitter_ms retry_jitter_ms;
    void *clock_context;
    uint8_t *request_frame;
    size_t request_frame_size;
    uint8_t *response_frame;
    size_t response_frame_size;
    uint64_t next_operation_id;
    uint32_t request_timeout_ms;
    uint16_t last_status;
    uint16_t last_result_flags;
    uint32_t last_retry_after_ms;
    void *pubsub_subscriptions;
    uint16_t pubsub_subscription_capacity;
    uint32_t pubsub_delivery_events;
    uint32_t pubsub_unknown_deliveries;
    uint32_t pubsub_delivery_sequence_gaps;
    uint8_t *pubsub_delivery_data;
    uint32_t pubsub_delivery_subscription_id;
    uint32_t pubsub_delivery_sequence;
    uint32_t pubsub_delivery_data_length;
    uint32_t pubsub_delivery_data_received;
    uint32_t retries;
    uint8_t consecutive_health_timeouts;
} robusto_proxy_client_t;

/** Initializes a proxy client instance from config. */
rob_ret_val_t robusto_proxy_client_init(
    robusto_proxy_client_t *client,
    const robusto_proxy_client_config_t *config);

/** Performs HELLO negotiation and establishes the proxy session. */
rob_ret_val_t robusto_proxy_client_connect(robusto_proxy_client_t *client);

/** Queries negotiated remote capabilities for the active session. */
rob_ret_val_t robusto_proxy_client_query_capabilities(
    robusto_proxy_client_t *client,
    robusto_proxy_capability_response_t *response);

/** Queries delegate health counters and uptime. */
rob_ret_val_t robusto_proxy_client_query_health(
    robusto_proxy_client_t *client,
    robusto_proxy_health_response_t *response);

/**
 * Queries delegate system identity and memory status.
 *
 * On ESP32-C6 delegates, delegate_version resolves in this order:
 * 1) CONFIG_ROBUSTO_PROXY_C6_DELEGATE_IDENTITY
 * 2) delegate ELF SHA-256 as 64 uppercase hexadecimal characters
 * 3) ROBUSTO_VERSION fallback
 */
rob_ret_val_t robusto_proxy_client_query_system_info(
    robusto_proxy_client_t *client,
    robusto_proxy_system_info_response_t *response);

/**
 * Requests a controlled reboot of the delegate.
 *
 * After success, callers must reconnect and renegotiate session state.
 */
rob_ret_val_t robusto_proxy_client_reboot_delegate(
    robusto_proxy_client_t *client);

rob_ret_val_t robusto_proxy_status_to_robusto(uint16_t status);

rob_ret_val_t robusto_proxy_client_request(
    robusto_proxy_client_t *client,
    uint8_t domain,
    uint8_t opcode,
    const uint8_t *payload,
    size_t payload_size,
    bool mutation,
    const uint8_t **success_payload,
    size_t *success_payload_size);

uint64_t robusto_proxy_client_take_operation_id(robusto_proxy_client_t *client);

#ifdef __cplusplus
}
#endif