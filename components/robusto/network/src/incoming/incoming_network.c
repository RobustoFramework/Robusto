
/**
 * @file incoming_network.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Handling of incoming networking-control-related messaging.
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

#include "robusto_incoming.h"
#include "robusto_message.h"
#include "robusto_peer.h"

static char *incoming_network_log_prefix;

rob_ret_val_t robusto_handle_network(incoming_queue_item_t *queue_item)
{
    if (queue_item->message->binary_data_length > 1)
    {
        // First byte is the request identifier
        e_network_request_t request_identifier = queue_item->message->binary_data[0];
        // Handle a presentation
        if (request_identifier == NET_HI || request_identifier == NET_HIR)
        {
            if (robusto_handle_presentation(queue_item->message) != ROB_OK)
            {
                ROB_LOGE(incoming_network_log_prefix, "Failed handling the presentation.");
                return ROB_FAIL;
            }
            else
            {
                return ROB_OK;
            }
        }
        switch (request_identifier)
        {
        default:
            ROB_LOGE(incoming_network_log_prefix, "Protocol error: Invalid network request - %i", request_identifier);
            // TODO: Setting something to suspect might un-ban a peer, we should probably have some function for setting the security related states. Also a peer should not be suspect, but the connection.
            queue_item->message->peer->state = PEER_KNOWN_SUSPECT;
        }
    }
    return ROB_FAIL;
}

rob_ret_val_t robusto_incoming_network_init(char *_log_prefix)
{
    incoming_network_log_prefix = _log_prefix;
    return ROB_OK;
}