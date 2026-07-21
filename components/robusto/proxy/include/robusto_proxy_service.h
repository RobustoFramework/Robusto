#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_inflight.h"
#include "robusto_proxy_pubsub.h"
#include "robusto_proxy_session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robusto_proxy_pubsub_adapter {
    uint16_t (*publish)(void *context,
                        const robusto_proxy_pubsub_publish_request_t *request,
                        robusto_proxy_pubsub_publish_response_t *response);
    uint16_t (*subscribe)(void *context,
                          const robusto_proxy_pubsub_subscribe_request_t *request,
                          robusto_proxy_pubsub_subscribe_response_t *response);
    uint16_t (*unsubscribe)(void *context,
                            const robusto_proxy_pubsub_unsubscribe_request_t *request,
                            robusto_proxy_pubsub_unsubscribe_response_t *response);
    uint16_t (*status)(void *context,
                       robusto_proxy_pubsub_status_response_t *response);
} robusto_proxy_pubsub_adapter_t;

typedef struct robusto_proxy_service {
    robusto_proxy_session_t session;
    robusto_proxy_inflight_table_t inflight;
    uint32_t started_at_ms;
    uint32_t requests;
    uint32_t events;
    uint32_t errors;
    uint32_t rx_crc_errors;
    uint32_t queue_high_water;
    const robusto_proxy_pubsub_adapter_t *pubsub_adapter;
    void *pubsub_adapter_context;
} robusto_proxy_service_t;

void robusto_proxy_service_init(
    robusto_proxy_service_t *service,
    robusto_proxy_profile_t profile,
    uint64_t local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed,
    uint16_t negotiated_inflight,
    uint32_t now_ms);

void robusto_proxy_service_set_pubsub_adapter(
    robusto_proxy_service_t *service,
    const robusto_proxy_pubsub_adapter_t *adapter,
    void *context);

uint16_t robusto_proxy_service_tick(
    robusto_proxy_service_t *service,
    uint32_t now_ms);

bool robusto_proxy_service_build_health_response(
    const robusto_proxy_service_t *service,
    uint32_t now_ms,
    robusto_proxy_health_response_t *response);

bool robusto_proxy_service_handle_control_request(
    robusto_proxy_service_t *service,
    uint8_t opcode,
    const uint8_t *request_payload,
    size_t request_payload_size,
    uint32_t now_ms,
    uint8_t *response_buffer,
    size_t response_buffer_size,
    size_t *response_size);

bool robusto_proxy_service_handle_pubsub_request(
    robusto_proxy_service_t *service,
    uint8_t opcode,
    const uint8_t *request_payload,
    size_t request_payload_size,
    uint8_t *response_buffer,
    size_t response_buffer_size,
    size_t *response_size);

robusto_proxy_result_t robusto_proxy_service_build_pubsub_delivery_event(
    robusto_proxy_service_t *service,
    const uint8_t *delivery_payload,
    size_t delivery_payload_size,
    uint8_t *event_frame,
    size_t event_frame_size,
    size_t *encoded_size);

robusto_proxy_result_t robusto_proxy_service_handle_frame(
    robusto_proxy_service_t *service,
    const uint8_t *request_frame,
    size_t request_frame_size,
    uint32_t now_ms,
    uint8_t *response_frame,
    size_t response_frame_size,
    size_t *response_size);

#ifdef __cplusplus
}
#endif
