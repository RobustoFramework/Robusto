/**
 * @file robusto_network_service.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto service functionality
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
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

#pragma once


#include "robusto_incoming.h"
#ifdef __cplusplus
extern "C"
{
#endif

// The services callback
typedef void(service_cb)(robusto_message_t *message);
// Service shutdown callback
typedef void(shutdown_cb)(void);

/**
 * @brief The service contains information that the service wants to provide
 * Freed by the service itself on shutdown. 
 * TODO: This should be defined on all structs
 */
typedef struct network_service
{
    // The id of the service, as referenced in communication
    uint16_t service_id;
    // The name of the service
    char * service_name;
    // The callback for incoming message
    service_cb * incoming_callback;
    // The service takes responsibility for freeing memory
    bool service_frees_message;
    // The callback for shutting down the service
    shutdown_cb * shutdown_callback;
} network_service_t;

// TODO: Should we allow several serviceids? It is kind of usable in a way. Then you can use the message in its whole. Or mabe use the binary part for that?

/**
 * @brief Register a service 
 * 
 * @param service A struct containing all important information
 */
rob_ret_val_t robusto_register_network_service(network_service_t * service);
/**
 * @brief Shutdown services
 * 
 */
void robusto_shutdown_network_services(void);

void robusto_network_service_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif