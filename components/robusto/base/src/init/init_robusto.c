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

typedef struct service
{
    init_callback_t *init_cb;
    start_callback_t *start_cb;
    stop_callback_t *stop_cb;
    uint8_t runlevel;
    char *service_name;
    SLIST_ENTRY(service)
    services;
} service_t;

/* Expands to a declaration for the work queue */
SLIST_HEAD(service_list, service)
service_list_l;

bool init_initiated = false;

char *robusto_log_prefix = "NOINIT";

uint8_t curr_runlevel = 0;

uint8_t get_runlevel()
{
    return curr_runlevel;
}

void register_service(init_callback_t init_cb, start_callback_t *start_cb, stop_callback_t *stop_cb, uint8_t runlevel, char *service_name)
{
    if (!init_initiated)
    {

        robusto_log_prefix = CONFIG_ROBUSTO_PEER_NAME;
        ROB_LOGI(robusto_log_prefix, "*** Initialize service list! ***");
        SLIST_INIT(&service_list_l);
        init_initiated = true;
    }

    service_t *new_service = robusto_malloc(sizeof(service_t));
    new_service->init_cb = init_cb;
    new_service->start_cb = start_cb;
    new_service->stop_cb = stop_cb;
    new_service->runlevel = runlevel;
    new_service->service_name = service_name;
    SLIST_INSERT_HEAD(&service_list_l, new_service, services);
    ROB_LOGI(robusto_log_prefix, "*** Added service %s ***", service_name);
};

void init_services(char *_log_prefix)
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
    }
}
void start_services(uint8_t runlevel)
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
    }
    curr_runlevel = runlevel;
    ROB_LOGI(robusto_log_prefix, "*************** Runlevel %hhu reached ***************", runlevel);
}

void stop_services()
{
    ROB_LOGI(robusto_log_prefix, "******************* Stopping services *******************");
    struct service *curr_service;
    SLIST_FOREACH(curr_service, &service_list_l, services)
    {
        if (curr_service->stop_cb)
        {
            ROB_LOGI(robusto_log_prefix, "Stopping service %s..", curr_service->service_name);
            curr_service->stop_cb();
        }
    }
}

void init_robusto()
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
    init_services(robusto_log_prefix);

    for (uint8_t runlevel = 2; runlevel < 6; runlevel++)
    {
        start_services(runlevel);
    }

 
    ROB_LOGI(robusto_log_prefix, "***************** Robusto running *****************");
}