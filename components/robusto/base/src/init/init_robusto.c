/**
 * @file init_robusto.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Initialization for Robusto.
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robusto_init.h>
#include <robusto_system.h>
#ifdef CONFIG_ROBUSTO_SLEEP
#include <robusto_sleep.h>
#endif
#include <robusto_logging.h>
#include <robusto_init_internal.h>
#include <robusto_sys_queue.h>
#include <robusto_time.h>
typedef struct service
{
    init_callback_t *init_cb;
    start_callback_t *start_cb;
    stop_callback_t *stop_cb;
    init_result_callback_t *init_result_cb;
    start_result_callback_t *start_result_cb;
    stop_result_callback_t *stop_result_cb;
    void *context;
    uint8_t runlevel;
    char *service_name;
    SLIST_ENTRY(service)
    services;
} service_t;

/* Expands to a declaration for the work queue */
SLIST_HEAD(service_list, service)
service_list_l;

bool init_initiated = false;
static rob_ret_val_t registration_result = ROB_OK;

char *robusto_log_prefix = "NOINIT";

uint8_t curr_runlevel = 0;

uint8_t get_runlevel()
{
    return curr_runlevel;
}

static rob_ret_val_t register_service_entry(
    init_callback_t *init_cb,
    start_callback_t *start_cb,
    stop_callback_t *stop_cb,
    init_result_callback_t *init_result_cb,
    start_result_callback_t *start_result_cb,
    stop_result_callback_t *stop_result_cb,
    void *context,
    uint8_t runlevel,
    char *service_name)
{
    if (service_name == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!init_initiated)
    {

        robusto_log_prefix = CONFIG_ROBUSTO_PEER_NAME;
        ROB_LOGI(robusto_log_prefix, "*** Initialize service list! ***");
        SLIST_INIT(&service_list_l);
        init_initiated = true;
    }

    service_t *new_service = robusto_malloc(sizeof(service_t));
    if (new_service == NULL)
    {
        if (registration_result == ROB_OK)
        {
            registration_result = ROB_ERR_OUT_OF_MEMORY;
        }
        ROB_LOGE(robusto_log_prefix, "Failed to register service %s: out of memory", service_name);
        return ROB_ERR_OUT_OF_MEMORY;
    }
    new_service->init_cb = init_cb;
    new_service->start_cb = start_cb;
    new_service->stop_cb = stop_cb;
    new_service->init_result_cb = init_result_cb;
    new_service->start_result_cb = start_result_cb;
    new_service->stop_result_cb = stop_result_cb;
    new_service->context = context;
    new_service->runlevel = runlevel;
    new_service->service_name = service_name;
    SLIST_INSERT_HEAD(&service_list_l, new_service, services);
    ROB_LOGI(robusto_log_prefix, "*** Added service %s ***", service_name);
    return ROB_OK;
}

void register_service(init_callback_t init_cb, start_callback_t *start_cb, stop_callback_t *stop_cb, uint8_t runlevel, char *service_name)
{
    rob_ret_val_t result = register_service_entry(init_cb, start_cb, stop_cb,
                                                  NULL, NULL, NULL, NULL,
                                                  runlevel, service_name);
    if (result != ROB_OK && registration_result == ROB_OK)
    {
        registration_result = result;
        ROB_LOGE(robusto_log_prefix, "Failed to register service");
    }
}

rob_ret_val_t register_service_checked(init_result_callback_t *init_cb,
                                       start_result_callback_t *start_cb,
                                       stop_result_callback_t *stop_cb,
                                       void *context,
                                       uint8_t runlevel,
                                       char *service_name)
{
    if (init_cb == NULL && start_cb == NULL && stop_cb == NULL)
    {
        if (registration_result == ROB_OK)
        {
            registration_result = ROB_ERR_INVALID_ARG;
        }
        return ROB_ERR_INVALID_ARG;
    }
    rob_ret_val_t result = register_service_entry(NULL, NULL, NULL,
                                                  init_cb, start_cb, stop_cb, context,
                                                  runlevel, service_name);
    if (result != ROB_OK && registration_result == ROB_OK)
    {
        registration_result = result;
    }
    return result;
}

static rob_ret_val_t init_services_checked(char *_log_prefix)
{
    ROB_LOGI(robusto_log_prefix, "****************** Initialize services ******************");
    struct service *curr_service;
    SLIST_FOREACH(curr_service, &service_list_l, services)
    {
        if (curr_service->init_cb)
        {
            ROB_LOGI(robusto_log_prefix, "Initializing service %s..", curr_service->service_name);
            curr_service->init_cb(_log_prefix);
        }
        else if (curr_service->init_result_cb)
        {
            ROB_LOGI(robusto_log_prefix, "Initializing service %s..", curr_service->service_name);
            rob_ret_val_t result = curr_service->init_result_cb(curr_service->context, _log_prefix);
            if (result != ROB_OK)
            {
                ROB_LOGE(robusto_log_prefix, "Service %s initialization failed: %d",
                         curr_service->service_name, result);
                return result;
            }
        }
    }
    return ROB_OK;
}

static rob_ret_val_t start_services_checked(uint8_t runlevel)
{
    ROB_LOGI(robusto_log_prefix, "*************** Starting runlevel %hhu **************", runlevel);
    struct service *curr_service;
    uint64_t mem_before;
    uint64_t time_before;
    SLIST_FOREACH(curr_service, &service_list_l, services)
    {
        if ((curr_service->start_cb) && (curr_service->runlevel == runlevel))
        {
            ROB_LOGW(robusto_log_prefix, "Starting service %s..", curr_service->service_name);

            mem_before = get_free_mem();
            time_before = r_millis();
            curr_service->start_cb();

            ROB_LOGW(robusto_log_prefix, "Started, took service %s %llu ms and %llu bytes.", curr_service->service_name, r_millis() - time_before, mem_before - get_free_mem());
        }
        else if ((curr_service->start_result_cb) && (curr_service->runlevel == runlevel))
        {
            ROB_LOGW(robusto_log_prefix, "Starting service %s..", curr_service->service_name);
            mem_before = get_free_mem();
            time_before = r_millis();
            rob_ret_val_t result = curr_service->start_result_cb(curr_service->context);
            if (result != ROB_OK)
            {
                ROB_LOGE(robusto_log_prefix, "Service %s start failed: %d",
                         curr_service->service_name, result);
                return result;
            }
            ROB_LOGW(robusto_log_prefix, "Started, took service %s %llu ms and %llu bytes.", curr_service->service_name, r_millis() - time_before, mem_before - get_free_mem());
        }
    }
    curr_runlevel = runlevel;
    ROB_LOGI(robusto_log_prefix, "*************** Runlevel %hhu reached ***************", runlevel);
    return ROB_OK;
}

rob_ret_val_t stop_robusto_checked(void)
{
    rob_ret_val_t first_error = ROB_OK;

    ROB_LOGI(robusto_log_prefix, "******************* Stopping services *******************");
    struct service *curr_service;
    SLIST_FOREACH(curr_service, &service_list_l, services)
    {
        if (curr_service->stop_cb)
        {
            ROB_LOGI(robusto_log_prefix, "Stopping service %s..", curr_service->service_name);
            curr_service->stop_cb();
        }
        else if (curr_service->stop_result_cb)
        {
            ROB_LOGI(robusto_log_prefix, "Stopping service %s..", curr_service->service_name);
            rob_ret_val_t result = curr_service->stop_result_cb(curr_service->context);
            if (result != ROB_OK)
            {
                ROB_LOGE(robusto_log_prefix, "Service %s stop failed: %d",
                         curr_service->service_name, result);
                if (first_error == ROB_OK)
                {
                    first_error = result;
                }
            }
        }
    }
    return first_error;
}

rob_ret_val_t init_robusto_checked(void)
{
    // Initialize base functionality
    robusto_log_prefix = CONFIG_ROBUSTO_PEER_NAME;
    robusto_system_init(robusto_log_prefix);
    robusto_flash_init(robusto_log_prefix);
    #ifdef CONFIG_ROBUSTO_SLEEP
    robusto_sleep_init(robusto_log_prefix);
    #endif
    robusto_init_compatibility();

    /* Register services */
    register_network_service();
    register_server_service();

    #ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT
    robusto_conductor_client_register_service();
    #endif
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_register_service();
    #endif

    register_misc_service();
    #ifdef CONFIG_ROBUSTO_INPUT
    register_input_service();
    #endif
    if (registration_result != ROB_OK)
    {
        return registration_result;
    }
    rob_ret_val_t result = init_services_checked(robusto_log_prefix);
    if (result != ROB_OK)
    {
        return result;
    }

    for (uint8_t runlevel = 1; runlevel < 6; runlevel++)
    {
        result = start_services_checked(runlevel);
        if (result != ROB_OK)
        {
            return result;
        }
    }

 
    ROB_LOGI(robusto_log_prefix, "***************** Robusto running *****************");
    return ROB_OK;
}

void init_robusto()
{
    rob_ret_val_t result = init_robusto_checked();
    if (result != ROB_OK)
    {
        ROB_LOGE(robusto_log_prefix, "Robusto initialization failed: %d", result);
    }
}