#pragma once

#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_PUBSUB_MAX_TOPIC_BYTES 255U
#define ROBUSTO_PROXY_PUBSUB_MAX_INLINE_DATA_BYTES 3824U
#define ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES 4080U
#define ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES 3824U
#define ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES 0x01U

#define ROBUSTO_PROXY_PUBSUB_PUBLISH_HEADER_SIZE_BYTES 16U
#define ROBUSTO_PROXY_PUBSUB_PUBLISH_BEGIN_HEADER_SIZE_BYTES 16U
#define ROBUSTO_PROXY_PUBSUB_PUBLISH_CHUNK_HEADER_SIZE_BYTES 16U
#define ROBUSTO_PROXY_PUBSUB_PUBLISH_TRANSFER_REQUEST_SIZE_BYTES 8U
#define ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_HEADER_SIZE_BYTES 12U
#define ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_REQUEST_SIZE_BYTES 12U
#define ROBUSTO_PROXY_PUBSUB_PUBLISH_RESPONSE_SIZE_BYTES 8U
#define ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_RESPONSE_SIZE_BYTES 12U
#define ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_RESPONSE_SIZE_BYTES 4U
#define ROBUSTO_PROXY_PUBSUB_STATUS_RESPONSE_SIZE_BYTES 36U
#define ROBUSTO_PROXY_PUBSUB_DELIVERY_HEADER_SIZE_BYTES 12U

typedef struct robusto_proxy_pubsub_publish_request {
    uint64_t operation_id;
    const uint8_t *topic;
    uint16_t topic_length;
    const uint8_t *data;
    uint32_t data_length;
} robusto_proxy_pubsub_publish_request_t;

typedef struct robusto_proxy_pubsub_publish_begin_request {
    uint64_t operation_id;
    const uint8_t *topic;
    uint16_t topic_length;
    uint32_t data_length;
} robusto_proxy_pubsub_publish_begin_request_t;

typedef struct robusto_proxy_pubsub_publish_chunk_request {
    uint64_t operation_id;
    uint32_t offset;
    const uint8_t *data;
    uint32_t data_length;
} robusto_proxy_pubsub_publish_chunk_request_t;

typedef struct robusto_proxy_pubsub_publish_transfer_request {
    uint64_t operation_id;
} robusto_proxy_pubsub_publish_transfer_request_t;

typedef struct robusto_proxy_pubsub_subscribe_request {
    uint64_t operation_id;
    const uint8_t *topic;
    uint16_t topic_length;
    uint8_t options;
} robusto_proxy_pubsub_subscribe_request_t;

typedef struct robusto_proxy_pubsub_unsubscribe_request {
    uint64_t operation_id;
    uint32_t subscription_id;
} robusto_proxy_pubsub_unsubscribe_request_t;

typedef struct robusto_proxy_pubsub_publish_response {
    uint32_t topic_hash;
    uint32_t delivery_count;
} robusto_proxy_pubsub_publish_response_t;

typedef struct robusto_proxy_pubsub_subscribe_response {
    uint32_t subscription_id;
    uint32_t topic_hash;
    uint8_t created;
} robusto_proxy_pubsub_subscribe_response_t;

typedef struct robusto_proxy_pubsub_unsubscribe_response {
    uint8_t removed;
} robusto_proxy_pubsub_unsubscribe_response_t;

typedef struct robusto_proxy_pubsub_status_response {
    uint8_t state;
    uint32_t active_subscriptions;
    uint32_t publish_requests;
    uint32_t subscribe_requests;
    uint32_t unsubscribe_requests;
    uint32_t delivery_events;
    uint32_t delivery_drops;
    uint32_t duplicate_operations;
    uint32_t pubsub_errors;
} robusto_proxy_pubsub_status_response_t;

typedef struct robusto_proxy_pubsub_delivery {
    uint32_t subscription_id;
    uint32_t delivery_sequence;
    const uint8_t *data;
    uint32_t data_length;
} robusto_proxy_pubsub_delivery_t;

bool robusto_proxy_pubsub_topic_is_valid(const uint8_t *topic, uint16_t length);

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_request_t *request, size_t *encoded_size);
robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_begin_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_begin_request_t *request, size_t *encoded_size);
robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_begin_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_begin_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_chunk_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_chunk_request_t *request, size_t *encoded_size);
robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_chunk_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_chunk_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_transfer_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_transfer_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_transfer_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_transfer_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_encode_subscribe_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_subscribe_request_t *request, size_t *encoded_size);
robusto_proxy_result_t robusto_proxy_pubsub_decode_subscribe_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_subscribe_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_encode_unsubscribe_request(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_unsubscribe_request_t *request);
robusto_proxy_result_t robusto_proxy_pubsub_decode_unsubscribe_request(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_unsubscribe_request_t *request);

robusto_proxy_result_t robusto_proxy_pubsub_encode_publish_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_publish_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_decode_publish_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_publish_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_encode_subscribe_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_subscribe_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_decode_subscribe_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_subscribe_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_encode_unsubscribe_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_unsubscribe_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_decode_unsubscribe_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_unsubscribe_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_encode_status_response(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_status_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_decode_status_response(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_status_response_t *response);
robusto_proxy_result_t robusto_proxy_pubsub_encode_delivery(
    uint8_t *buffer, size_t buffer_size,
    const robusto_proxy_pubsub_delivery_t *delivery, size_t *encoded_size);
robusto_proxy_result_t robusto_proxy_pubsub_decode_delivery(
    const uint8_t *buffer, size_t buffer_size,
    robusto_proxy_pubsub_delivery_t *delivery);

#ifdef __cplusplus
}
#endif