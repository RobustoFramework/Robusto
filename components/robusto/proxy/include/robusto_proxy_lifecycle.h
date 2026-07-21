#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <robusto_retval.h>

#include "robusto_proxy_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef rob_ret_val_t (*robusto_proxy_transport_init_callback)(
    void *context,
    char *log_prefix);
typedef rob_ret_val_t (*robusto_proxy_transport_start_callback)(void *context);
typedef rob_ret_val_t (*robusto_proxy_transport_stop_callback)(void *context);

typedef struct robusto_proxy_client_service_config {
    robusto_proxy_client_t *client;
    robusto_proxy_client_config_t client_config;
    robusto_proxy_transport_init_callback transport_init;
    robusto_proxy_transport_start_callback transport_start;
    robusto_proxy_transport_stop_callback transport_stop;
    void *transport_lifecycle_context;
    uint8_t runlevel;
} robusto_proxy_client_service_config_t;

typedef struct robusto_proxy_client_service {
    robusto_proxy_client_service_config_t config;
    bool registered;
    bool transport_initialized;
    bool transport_started;
} robusto_proxy_client_service_t;

rob_ret_val_t robusto_proxy_client_service_register(
    robusto_proxy_client_service_t *service,
    const robusto_proxy_client_service_config_t *config);

bool robusto_proxy_client_service_is_ready(
    const robusto_proxy_client_service_t *service);

#ifdef __cplusplus
}
#endif