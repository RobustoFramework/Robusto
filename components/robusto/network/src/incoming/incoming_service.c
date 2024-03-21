
/**
 * @file incoming_service.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Handling of incoming service calls
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

#include "robusto_network_service.h"
#include "robusto_queue.h"
#include "robusto_retval.h"
char *incoming_service_log_prefix = "Before init";

uint8_t services_count = 0;

network_service_t *services[10]; 

// TODO: At least check so that not too many services are register, preferably also change to a linked list

rob_ret_val_t robusto_register_network_service(network_service_t * service) {
    if (service->incoming_callback == NULL) {

        ROB_LOGE(incoming_service_log_prefix, "Failed adding service, no incoming callback assigned.");
        return ROB_FAIL;
    }
    if (service->shutdown_callback == NULL) {
        ROB_LOGE(incoming_service_log_prefix, "Failed adding service, no shutdown callback assigned.");
        return ROB_FAIL;
    }
    services[services_count] = service;
    services_count++;
    ROB_LOGI(incoming_service_log_prefix, "%s network service, successfully added. Service id: %i", service->service_name, service->service_id);
    return ROB_OK;
}

rob_ret_val_t robusto_handle_service(incoming_queue_item_t *queue_item) {
    rob_ret_val_t retval = ROB_FAIL;
    ROB_LOGD(incoming_service_log_prefix, "in robusto_handle_service, service_id: %u", queue_item->message->service_id);
    // First byte is the request identifier
    uint16_t service_id = queue_item->message->service_id;   
    // Find the service
    uint8_t curr_srv = 0;
    while (curr_srv < services_count) {

        if (services[curr_srv]->service_id == service_id) {
            ROB_LOGD(incoming_service_log_prefix, "Found a matching service"); 
            // Call the service
            services[curr_srv]->incoming_callback(queue_item->message);
            queue_item->recipient_frees_message = services[curr_srv]->service_frees_message;
            return ROB_OK;
        }
        curr_srv++;
    }
    ROB_LOGE(incoming_service_log_prefix, "Invalid service id (%"PRIu16")", service_id);
    return ROB_ERR_INVALID_SERVICE_ID;    
}

void robusto_network_service_init(char * _log_prefix) {

    incoming_service_log_prefix = _log_prefix;
}
