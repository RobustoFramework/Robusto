#include "robusto_proxy_lifecycle.h"

#include <string.h>

#include <robusto_init.h>

static rob_ret_val_t proxy_client_service_init(void *context, char *log_prefix)
{
    robusto_proxy_client_service_t *service = context;
    rob_ret_val_t result;

    result = service->config.transport_init(
        service->config.transport_lifecycle_context, log_prefix);
    if (result != ROB_OK)
    {
        return result;
    }
    service->transport_initialized = true;

    return robusto_proxy_client_init(service->config.client,
                                     &service->config.client_config);
}

static rob_ret_val_t proxy_client_service_start(void *context)
{
    robusto_proxy_client_service_t *service = context;
    rob_ret_val_t result;

    if (!service->transport_initialized)
    {
        return ROB_ERR_NOT_READY;
    }
    result = service->config.transport_start(
        service->config.transport_lifecycle_context);
    if (result != ROB_OK)
    {
        return result;
    }
    service->transport_started = true;

    return robusto_proxy_client_connect(service->config.client);
}

static rob_ret_val_t proxy_client_service_stop(void *context)
{
    robusto_proxy_client_service_t *service = context;
    rob_ret_val_t result;

    service->config.client->session.state = ROBUSTO_PROXY_SESSION_RESET;
    if (!service->transport_initialized)
    {
        return ROB_OK;
    }
    result = service->config.transport_stop(
        service->config.transport_lifecycle_context);
    if (result == ROB_OK)
    {
        service->transport_started = false;
        service->transport_initialized = false;
    }
    return result;
}

rob_ret_val_t robusto_proxy_client_service_register(
    robusto_proxy_client_service_t *service,
    const robusto_proxy_client_service_config_t *config)
{
    rob_ret_val_t result;

    if (service == NULL || config == NULL || config->client == NULL ||
        config->transport_init == NULL || config->transport_start == NULL ||
        config->transport_stop == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }

    memset(service, 0, sizeof(*service));
    service->config = *config;
    result = register_service_checked(
        proxy_client_service_init,
        proxy_client_service_start,
        proxy_client_service_stop,
        service,
        config->runlevel,
        "Proxy client");
    if (result == ROB_OK)
    {
        service->registered = true;
    }
    return result;
}

bool robusto_proxy_client_service_is_ready(
    const robusto_proxy_client_service_t *service)
{
    return service != NULL && service->registered && service->transport_started &&
           service->config.client != NULL &&
           service->config.client->session.state == ROBUSTO_PROXY_SESSION_ESTABLISHED;
}