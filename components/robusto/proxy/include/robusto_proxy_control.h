#pragma once

#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Encoded payload size for HELLO request. */
#define ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES 36U
/** Encoded payload size for a control response prefix. */
#define ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES 12U
/** Encoded payload size for HELLO response payload. */
#define ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES 40U
/** Encoded payload size for CAPABILITY response payload. */
#define ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES 28U
/** Encoded payload size for HEALTH response payload. */
#define ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES 40U
/** Encoded payload size for SYSTEM_INFO response payload. */
#define ROBUSTO_PROXY_SYSTEM_INFO_RESPONSE_SIZE_BYTES 116U
/** Encoded payload size for FRAGMENT_STATS response payload. */
#define ROBUSTO_PROXY_FRAGMENT_STATS_RESPONSE_SIZE_BYTES 188U

/** Encodes a control response prefix into buffer. */
robusto_proxy_result_t robusto_proxy_encode_response_prefix(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_response_prefix_t *prefix);

/** Decodes a control response prefix from buffer. */
robusto_proxy_result_t robusto_proxy_decode_response_prefix(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix);

/** Returns true when the decoded control status is ROBUSTO_PROXY_STATUS_OK. */
bool robusto_proxy_response_prefix_is_success(const robusto_proxy_response_prefix_t *prefix);

robusto_proxy_result_t robusto_proxy_encode_hello_request(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_hello_request_t *request);

robusto_proxy_result_t robusto_proxy_decode_hello_request(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_hello_request_t *request);

robusto_proxy_result_t robusto_proxy_encode_hello_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_hello_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_hello_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_hello_response_t *response);

robusto_proxy_result_t robusto_proxy_encode_capability_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_capability_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_capability_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_capability_response_t *response);

robusto_proxy_result_t robusto_proxy_encode_health_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_health_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_health_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_health_response_t *response);

/** Encodes SYSTEM_INFO response payload (without response prefix). */
robusto_proxy_result_t robusto_proxy_encode_system_info_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_system_info_response_t *response);

/** Decodes SYSTEM_INFO response payload (without response prefix). */
robusto_proxy_result_t robusto_proxy_decode_system_info_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_system_info_response_t *response);

/** Encodes FRAGMENT_STATS response payload (without response prefix). */
robusto_proxy_result_t robusto_proxy_encode_fragment_stats_response(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_fragment_stats_response_t *response);

/** Decodes FRAGMENT_STATS response payload (without response prefix). */
robusto_proxy_result_t robusto_proxy_decode_fragment_stats_response(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_fragment_stats_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_hello_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_hello_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_capability_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_capability_response_t *response);

robusto_proxy_result_t robusto_proxy_decode_health_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_health_response_t *response);

/** Decodes prefixed SYSTEM_INFO response message into prefix and payload. */
robusto_proxy_result_t robusto_proxy_decode_system_info_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_system_info_response_t *response);

/** Decodes prefixed FRAGMENT_STATS response message into prefix and payload. */
robusto_proxy_result_t robusto_proxy_decode_fragment_stats_response_message(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix,
    robusto_proxy_fragment_stats_response_t *response);

#ifdef __cplusplus
}
#endif
