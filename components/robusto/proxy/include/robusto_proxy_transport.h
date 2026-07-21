#pragma once

#include <stddef.h>
#include <stdint.h>

#include <robusto_retval.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robusto_proxy_transfer_acceptance {
    ROBUSTO_PROXY_TRANSFER_NOT_ACCEPTED = 0,
    ROBUSTO_PROXY_TRANSFER_ACCEPTED,
    ROBUSTO_PROXY_TRANSFER_ACCEPTANCE_UNKNOWN,
} robusto_proxy_transfer_acceptance_t;

typedef rob_ret_val_t (*robusto_proxy_transport_exchange)(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size,
    uint32_t timeout_ms,
    robusto_proxy_transfer_acceptance_t *acceptance);

#ifdef __cplusplus
}
#endif