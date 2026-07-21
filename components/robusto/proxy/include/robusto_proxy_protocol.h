#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_SLOT_SIZE_BYTES 4120U
#define ROBUSTO_PROXY_HEADER_SIZE_BYTES 20U
#define ROBUSTO_PROXY_CRC_SIZE_BYTES 4U
#define ROBUSTO_PROXY_MAX_PAYLOAD_BYTES 4096U

#define ROBUSTO_PROXY_PROTOCOL_MAJOR 1U
#define ROBUSTO_PROXY_PROTOCOL_MINOR 0U

#define ROBUSTO_PROXY_FLAG_REQUEST 0x01U
#define ROBUSTO_PROXY_FLAG_RESPONSE 0x02U
#define ROBUSTO_PROXY_FLAG_EVENT 0x04U
#define ROBUSTO_PROXY_FLAG_RETRY 0x08U

#define ROBUSTO_PROXY_DOMAIN_CONTROL 0x00U
#define ROBUSTO_PROXY_DOMAIN_PUBSUB 0x01U

#define ROBUSTO_PROXY_OPCODE_HELLO 0x01U
#define ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY 0x02U
#define ROBUSTO_PROXY_OPCODE_HEALTH 0x03U

#define ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH 0x01U
#define ROBUSTO_PROXY_PUBSUB_OPCODE_SUBSCRIBE 0x02U
#define ROBUSTO_PROXY_PUBSUB_OPCODE_UNSUBSCRIBE 0x03U
#define ROBUSTO_PROXY_PUBSUB_OPCODE_STATUS 0x04U
#define ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY 0x80U

#define ROBUSTO_PROXY_FEATURE_PUBSUB_V1 0x0000000000000001ULL

#define ROBUSTO_PROXY_STATUS_OK 0x0000U
#define ROBUSTO_PROXY_STATUS_INTERNAL 0x0001U
#define ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT 0x0002U
#define ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD 0x0003U
#define ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY 0x0004U
#define ROBUSTO_PROXY_STATUS_BUSY 0x0005U
#define ROBUSTO_PROXY_STATUS_TIMEOUT 0x0006U
#define ROBUSTO_PROXY_STATUS_NOT_READY 0x0007U
#define ROBUSTO_PROXY_STATUS_NOT_FOUND 0x0008U
#define ROBUSTO_PROXY_STATUS_CONFLICT 0x0009U
#define ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED 0x0101U
#define ROBUSTO_PROXY_STATUS_UNSUPPORTED_VERSION 0x0102U
#define ROBUSTO_PROXY_STATUS_UNSUPPORTED_DOMAIN 0x0103U
#define ROBUSTO_PROXY_STATUS_UNSUPPORTED_OPCODE 0x0104U
#define ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE 0x0105U
#define ROBUSTO_PROXY_STATUS_STALE_SESSION 0x0106U
#define ROBUSTO_PROXY_STATUS_TOO_MANY_IN_FLIGHT 0x0107U
#define ROBUSTO_PROXY_STATUS_LINK_CRC 0x0201U
#define ROBUSTO_PROXY_STATUS_LINK_LENGTH 0x0202U
#define ROBUSTO_PROXY_STATUS_LINK_IO 0x0203U
#define ROBUSTO_PROXY_STATUS_OUTCOME_UNKNOWN 0x0204U
#define ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID 0x0301U
#define ROBUSTO_PROXY_STATUS_PUBSUB_PAYLOAD_TOO_LARGE 0x0302U
#define ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_NOT_FOUND 0x0303U
#define ROBUSTO_PROXY_STATUS_PUBSUB_DELIVERY_FAILED 0x0304U
#define ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_LIMIT 0x0305U

#define ROBUSTO_PROXY_RESULT_FLAG_RETRYABLE 0x0001U

typedef enum robusto_proxy_result {
    ROBUSTO_PROXY_RESULT_OK = 0,
    ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT,
    ROBUSTO_PROXY_RESULT_BAD_MAGIC,
    ROBUSTO_PROXY_RESULT_BAD_VERSION,
    ROBUSTO_PROXY_RESULT_BAD_HEADER_LENGTH,
    ROBUSTO_PROXY_RESULT_BAD_FLAGS,
    ROBUSTO_PROXY_RESULT_BAD_RESERVED,
    ROBUSTO_PROXY_RESULT_BAD_LENGTH,
    ROBUSTO_PROXY_RESULT_BAD_CRC,
} robusto_proxy_result_t;

typedef enum robusto_proxy_session_state {
    ROBUSTO_PROXY_SESSION_RESET = 0,
    ROBUSTO_PROXY_SESSION_NEGOTIATING,
    ROBUSTO_PROXY_SESSION_ESTABLISHED,
    ROBUSTO_PROXY_SESSION_DEGRADED,
    ROBUSTO_PROXY_SESSION_INCOMPATIBLE,
} robusto_proxy_session_state_t;

typedef enum robusto_proxy_profile {
    ROBUSTO_PROXY_PROFILE_LOW_MEMORY = 0,
    ROBUSTO_PROXY_PROFILE_STANDARD,
} robusto_proxy_profile_t;

typedef struct robusto_proxy_frame_header {
    uint8_t magic[2];
    uint8_t protocol_major;
    uint8_t header_length;
    uint8_t flags;
    uint8_t domain;
    uint8_t opcode;
    uint8_t reserved;
    uint32_t correlation_id;
    uint32_t sequence;
    uint32_t payload_length;
} robusto_proxy_frame_header_t;

typedef struct robusto_proxy_hello_request {
    uint64_t controller_boot_id;
    uint8_t min_protocol_major;
    uint8_t max_protocol_major;
    uint8_t min_protocol_minor;
    uint8_t max_protocol_minor;
    uint32_t max_payload;
    uint16_t max_in_flight;
    uint16_t reserved;
    uint64_t required_features;
    uint64_t optional_features;
} robusto_proxy_hello_request_t;

typedef struct robusto_proxy_hello_response {
    uint64_t proxy_boot_id;
    uint8_t selected_protocol_major;
    uint8_t selected_protocol_minor;
    uint16_t reserved;
    uint32_t selected_max_payload;
    uint16_t selected_max_in_flight;
    uint16_t dedupe_entries;
    uint32_t dedupe_window_ms;
    uint64_t enabled_features;
    uint32_t proxy_uptime_ms;
    uint32_t reserved2;
} robusto_proxy_hello_response_t;

typedef struct robusto_proxy_capability_response {
    uint64_t proxy_boot_id;
    uint64_t enabled_features;
    uint8_t pubsub_major;
    uint8_t pubsub_minor;
    uint16_t reserved;
    uint32_t selected_max_payload;
    uint16_t selected_max_in_flight;
    uint16_t max_subscriptions;
} robusto_proxy_capability_response_t;

typedef struct robusto_proxy_health_response {
    uint64_t proxy_boot_id;
    uint32_t uptime_ms;
    uint8_t service_state;
    uint8_t reserved[3];
    uint32_t requests;
    uint32_t events;
    uint32_t errors;
    uint32_t timeouts;
    uint32_t rx_crc_errors;
    uint32_t queue_high_water;
} robusto_proxy_health_response_t;

typedef struct robusto_proxy_response_prefix {
    uint16_t status;
    uint16_t result_flags;
    uint32_t retry_after_ms;
    uint16_t detail_length;
    uint16_t reserved;
} robusto_proxy_response_prefix_t;

typedef struct robusto_proxy_profile_limits {
    uint16_t max_in_flight;
    uint16_t max_subscriptions;
    uint16_t dedupe_entries;
    uint32_t dedupe_window_ms;
    uint32_t request_pool_bytes;
    uint32_t response_pool_bytes;
} robusto_proxy_profile_limits_t;

bool robusto_proxy_flag_is_valid(uint8_t flags);
bool robusto_proxy_payload_length_is_valid(uint32_t payload_length);
size_t robusto_proxy_frame_size_bytes(uint32_t payload_length);
robusto_proxy_profile_limits_t robusto_proxy_profile_limits(robusto_proxy_profile_t profile);

#ifdef __cplusplus
}
#endif
