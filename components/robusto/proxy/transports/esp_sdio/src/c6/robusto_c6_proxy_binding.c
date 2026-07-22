#include "robusto_c6_proxy_binding.h"

#include "esp_err.h"
#include "robusto_c6_proxy_server.h"
#include "robusto_init.h"

static rob_ret_val_t map_bridge_error(esp_err_t error)
{
    switch (error) {
        case ESP_OK:
            return ROB_OK;
        case ESP_ERR_NO_MEM:
            return ROB_ERR_OUT_OF_MEMORY;
        case ESP_ERR_INVALID_ARG:
            return ROB_ERR_INVALID_ARG;
        case ESP_ERR_INVALID_STATE:
            return ROB_ERR_NOT_READY;
        case ESP_ERR_TIMEOUT:
            return ROB_ERR_TIMEOUT;
        default:
            return ROB_ERR_INIT_FAIL;
    }
}

static rob_ret_val_t proxy_service_init(void *context, char *log_prefix)
{
    (void)context;
    (void)log_prefix;
    return map_bridge_error(robusto_c6_proxy_server_init());
}

static rob_ret_val_t proxy_service_start(void *context)
{
    (void)context;
    return map_bridge_error(robusto_c6_proxy_server_start());
}

static rob_ret_val_t proxy_service_stop(void *context)
{
    (void)context;
    return map_bridge_error(robusto_c6_proxy_server_deinit());
}

rob_ret_val_t robusto_c6_proxy_service_register(void)
{
    return register_service_checked(proxy_service_init,
                                    proxy_service_start,
                                    proxy_service_stop,
                                    NULL,
                                    1U,
                                    "Proxy server");
}