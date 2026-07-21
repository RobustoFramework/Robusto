#pragma once

#include <stddef.h>
#include <stdint.h>

#include "robusto_proxy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES 36U
#define ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES 12U
#define ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES 40U
#define ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES 28U
#define ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES 40U

robusto_proxy_result_t robusto_proxy_encode_response_prefix(
    uint8_t *buffer,
    size_t buffer_size,
    const robusto_proxy_response_prefix_t *prefix);

robusto_proxy_result_t robusto_proxy_decode_response_prefix(
    const uint8_t *buffer,
    size_t buffer_size,
    robusto_proxy_response_prefix_t *prefix);

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

#ifdef __cplusplus
}
#endif
