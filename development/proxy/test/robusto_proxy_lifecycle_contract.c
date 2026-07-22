#include <robusto_proxy_lifecycle.h>

#include <stdio.h>
#include <string.h>

#include "support/robusto_init.h"

static int assertions;
static int failures;
static init_result_callback_t *registered_init;
static start_result_callback_t *registered_start;
static stop_result_callback_t *registered_stop;
static void *registered_context;
static rob_ret_val_t registration_result;
static rob_ret_val_t transport_init_result;
static rob_ret_val_t transport_start_result;
static rob_ret_val_t transport_stop_result;
static rob_ret_val_t client_init_result;
static rob_ret_val_t client_connect_result;

#define ASSERT_EQUAL(expected, actual) assert_equal((expected), (actual), #actual, __LINE__)
#define ASSERT_TRUE(actual) ASSERT_EQUAL(1, (actual) ? 1 : 0)
#define ASSERT_FALSE(actual) ASSERT_EQUAL(0, (actual) ? 1 : 0)

static void assert_equal(int expected, int actual, const char *expression, int line)
{
    ++assertions;
    if (expected != actual)
    {
        ++failures;
        fprintf(stderr, "FAIL line %d: %s expected %d got %d\n",
                line, expression, expected, actual);
    }
}

rob_ret_val_t register_service_checked(init_result_callback_t *init_cb,
                                       start_result_callback_t *start_cb,
                                       stop_result_callback_t *stop_cb,
                                       void *context,
                                       uint8_t runlevel,
                                       char *service_name)
{
    ASSERT_EQUAL(1, runlevel);
    ASSERT_TRUE(strcmp(service_name, "Proxy client") == 0);
    registered_init = init_cb;
    registered_start = start_cb;
    registered_stop = stop_cb;
    registered_context = context;
    return registration_result;
}

rob_ret_val_t robusto_proxy_client_init(
    robusto_proxy_client_t *client,
    const robusto_proxy_client_config_t *config)
{
    (void)client;
    (void)config;
    return client_init_result;
}

rob_ret_val_t robusto_proxy_client_connect(robusto_proxy_client_t *client)
{
    if (client_connect_result == ROB_OK)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    }
    return client_connect_result;
}

static rob_ret_val_t transport_init(void *context, char *log_prefix)
{
    ASSERT_EQUAL(42, *(int *)context);
    ASSERT_TRUE(strcmp(log_prefix, "test") == 0);
    return transport_init_result;
}

static rob_ret_val_t transport_start(void *context)
{
    ASSERT_EQUAL(42, *(int *)context);
    return transport_start_result;
}

static rob_ret_val_t transport_stop(void *context)
{
    ASSERT_EQUAL(42, *(int *)context);
    return transport_stop_result;
}

static robusto_proxy_client_service_config_t make_config(
    robusto_proxy_client_t *client,
    int *transport_context)
{
    robusto_proxy_client_service_config_t config;

    memset(&config, 0, sizeof(config));
    config.client = client;
    config.transport_init = transport_init;
    config.transport_start = transport_start;
    config.transport_stop = transport_stop;
    config.transport_lifecycle_context = transport_context;
    config.runlevel = 1U;
    return config;
}

int main(void)
{
    robusto_proxy_client_t client;
    robusto_proxy_client_service_t service;
    int transport_context = 42;
    robusto_proxy_client_service_config_t config =
        make_config(&client, &transport_context);

    memset(&client, 0, sizeof(client));
    ASSERT_EQUAL(ROB_ERR_INVALID_ARG,
                 robusto_proxy_client_service_register(NULL, &config));
    config.transport_start = NULL;
    ASSERT_EQUAL(ROB_ERR_INVALID_ARG,
                 robusto_proxy_client_service_register(&service, &config));
    config.transport_start = transport_start;

    registration_result = ROB_ERR_OUT_OF_MEMORY;
    ASSERT_EQUAL(ROB_ERR_OUT_OF_MEMORY,
                 robusto_proxy_client_service_register(&service, &config));
    ASSERT_FALSE(service.registered);

    registration_result = ROB_OK;
    ASSERT_EQUAL(ROB_OK,
                 robusto_proxy_client_service_register(&service, &config));
    ASSERT_TRUE(service.registered);
    ASSERT_TRUE(registered_context == &service);

    transport_init_result = ROB_ERR_INIT_FAIL;
    ASSERT_EQUAL(ROB_ERR_INIT_FAIL, registered_init(registered_context, "test"));
    ASSERT_FALSE(service.transport_initialized);

    transport_init_result = ROB_OK;
    client_init_result = ROB_ERR_INVALID_ARG;
    ASSERT_EQUAL(ROB_ERR_INVALID_ARG, registered_init(registered_context, "test"));
    ASSERT_TRUE(service.transport_initialized);

    service.transport_initialized = false;
    ASSERT_EQUAL(ROB_ERR_NOT_READY, registered_start(registered_context));
    service.transport_initialized = true;
    transport_start_result = ROB_ERR_TIMEOUT;
    ASSERT_EQUAL(ROB_ERR_TIMEOUT, registered_start(registered_context));
    ASSERT_FALSE(service.transport_started);

    transport_start_result = ROB_OK;
    client_connect_result = ROB_ERR_NOT_SUPPORTED;
    ASSERT_EQUAL(ROB_ERR_NOT_SUPPORTED, registered_start(registered_context));
    ASSERT_TRUE(service.transport_started);
    ASSERT_FALSE(robusto_proxy_client_service_is_ready(&service));

    client_connect_result = ROB_OK;
    ASSERT_EQUAL(ROB_OK, registered_start(registered_context));
    ASSERT_TRUE(robusto_proxy_client_service_is_ready(&service));

    transport_stop_result = ROB_ERR_SEND_FAIL;
    ASSERT_EQUAL(ROB_ERR_SEND_FAIL, registered_stop(registered_context));
    ASSERT_FALSE(robusto_proxy_client_service_is_ready(&service));
    ASSERT_TRUE(service.transport_started);

    transport_stop_result = ROB_OK;
    ASSERT_EQUAL(ROB_OK, registered_stop(registered_context));
    ASSERT_FALSE(service.transport_initialized);
    ASSERT_FALSE(service.transport_started);

    printf("Proxy lifecycle contract: %d assertions, %d failures\n",
           assertions, failures);
    return failures == 0 ? 0 : 1;
}