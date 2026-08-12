
/**
 * @file incoming.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Handling of incoming messaging - were the media put incoming messages
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
#include <robusto_network_init.h>
#include <robusto_incoming.h>
#include <robusto_network_service.h>
#include <robusto_queue.h>
#include <robusto_peer.h>
#include <robusto_media.h>
#include <robusto_logging.h>
#include <string.h>

#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER) || defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include <robusto_pubsub.h>
#define ROBUSTO_TRACE_PUBSUB_MESSAGES 1
#else
#define ROBUSTO_TRACE_PUBSUB_MESSAGES 0
#endif

#ifdef CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#endif
static char *incoming_log_prefix;

static incoming_callback_cb *incoming_callback = NULL;

static bool is_large_pubsub_publish(const robusto_message_t *message)
{
#if !ROBUSTO_TRACE_PUBSUB_MESSAGES
    (void)message;
    return false;
#else
    return message != NULL &&
           message->context.is_service_call &&
           message->service_id == PUBSUB_SERVER_ID &&
           message->binary_data != NULL &&
           message->binary_data_length > 5U &&
           message->binary_data[0] == PUBSUB_PUBLISH &&
           (message->binary_data_length - 5U) > 8192U;
#endif
}

static void log_large_pubsub_message(const char *phase, const robusto_message_t *message)
{
    uint32_t topic_hash = 0U;
    uint32_t payload_length = 0U;

    if (!is_large_pubsub_publish(message)) {
        return;
    }

    memcpy(&topic_hash, message->binary_data + 1, sizeof(topic_hash));
    payload_length = message->binary_data_length - 5U;

    ROB_LOGW(incoming_log_prefix,
             "Large PubSub %s peer=%s media=%hhu topic_hash=%lu payload=%lu raw=%lu conv=%u",
             phase,
             message->peer->name,
             message->media_type,
             (unsigned long)topic_hash,
             (unsigned long)payload_length,
             (unsigned long)message->raw_data_length,
             message->conversation_id);
}

rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t data_length, robusto_peer_t *peer, e_media_type media_type, int offset)
{
    // TODO: Messages might be too large for SRAM and might need to be stored elsewhere (flash), how to handle? We might also have to *send* large files.
    // TODO: As this might be done inside receive callback, it should possibly spawn a task instead
    // rob_log_bit_mesh(ROB_LOG_WARN, incoming_log_prefix, data + offset, data_length - offset);
    ROB_LOGD(incoming_log_prefix, "In robusto_handle_incoming.");
    robusto_message_t *message;
// Parse and check the message
#ifdef CONFIG_HEAP_TRACING_STANDALONE
    ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
#endif
    rob_ret_val_t parse_retval = robusto_network_parse_message(data, data_length, peer, &message, offset);

#ifdef CONFIG_HEAP_TRACING_STANDALONE
    ESP_ERROR_CHECK(heap_trace_stop());
    heap_trace_dump();
#endif
    if (parse_retval < 0)
    {
        ROB_LOGE(incoming_log_prefix, "robusto_network_parse_message returned an error: %i", parse_retval);

        // A CRC32-checked message should never fail parsing, suspect foul play, deploy countermeasures.
        peer->state = PEER_KNOWN_SUSPECT;
        robusto_free(data);
        return ROB_FAIL;
    }

    message->media_type = media_type;
    log_large_pubsub_message("parsed", message);

    // Add it to the work queue
    incoming_queue_item_t *queue_item = robusto_malloc(sizeof(incoming_queue_item_t));
    if (queue_item == NULL)
    {
        ROB_LOGE(incoming_log_prefix, "Failed allocating incoming queue item.");
        robusto_message_free(message);
        return ROB_ERR_OUT_OF_MEMORY;
    }
    queue_item->message = message;

    rob_ret_val_t queue_retval = incoming_safe_add_work_queue(queue_item);
    if (queue_retval != ROB_OK)
    {
        log_large_pubsub_message("queue_rejected", message);
        ROB_LOGW(incoming_log_prefix,
                 "Failed adding incoming message to queue, retval=%i peer=%s media=%hhu raw=%lu",
                 queue_retval,
                 peer->name,
                 media_type,
                 (unsigned long)data_length);
        robusto_message_free(message);
        robusto_free(queue_item);
        return queue_retval;
    }
    log_large_pubsub_message("queued", message);
    ROB_LOGD(incoming_log_prefix, "Added a message to the incoming queue.");

    return ROB_OK;
}

void incoming_do_on_incoming_cb(incoming_queue_item_t *queue_item)
{

    ROB_LOGD(incoming_log_prefix, "In incoming_do_on_incoming_cb");
    log_large_pubsub_message("worker_begin", queue_item->message);
    queue_item->recipient_frees_message = false;
    if (queue_item->message->context.message_type == MSG_NETWORK)
    {
        ROB_LOGD(incoming_log_prefix, "Is network request");
        // Handle network requests
        robusto_handle_network(queue_item);
    }
    else if (queue_item->message->context.is_service_call)
    {
        robusto_handle_service(queue_item);
    }
    else
    {
        // If it is to the app, call the app_on_incoming_cb
        if (incoming_callback != NULL)
        {
            incoming_callback(queue_item);
        }
        else
        {
            ROB_LOGE(incoming_log_prefix, "No incoming callback defined for this message from %s, media_type %hu", queue_item->message->peer->name, queue_item->message->media_type);
            rob_log_bit_mesh(ROB_LOG_ERROR, incoming_log_prefix, queue_item->message->raw_data, queue_item->message->raw_data_length > 20 ? 20: queue_item->message->raw_data_length );
        }
    }
    log_large_pubsub_message("worker_end", queue_item->message);
    if (!queue_item->recipient_frees_message)
    {
        if (strcmp(queue_item->message->peer->name, "") == 0)
        {
            ROB_LOGD(incoming_log_prefix, "Freeing message from null-named peer, type %hhu.", queue_item->message->media_type);
        }
        else
        {
            ROB_LOGD(incoming_log_prefix, "Freeing message from %s, type %hhu", queue_item->message->peer->name, queue_item->message->media_type);
        }
        ROB_LOGD(incoming_log_prefix, "Message to be freed : ");
        rob_log_bit_mesh(ROB_LOG_DEBUG, incoming_log_prefix, queue_item->message->raw_data, queue_item->message->raw_data_length);
        robusto_message_free(queue_item->message);
    }

    robusto_free(queue_item);
}
void incoming_do_on_poll_cb(queue_context_t *q_context)
{
}

rob_ret_val_t robusto_register_handler(incoming_callback_cb *_incoming_callback)
{
    incoming_callback = _incoming_callback;
    return ROB_OK;
}

void robusto_incoming_start()
{
    ROB_LOGI(incoming_log_prefix, "Starting incoming queue");

    if (incoming_init_worker(&incoming_do_on_incoming_cb, incoming_log_prefix) != ROB_OK)
    {
        ROB_LOGE(incoming_log_prefix, "Failed initializing incoming queue worker");
        return;
    }
    incoming_set_queue_blocked(false);

    ROB_LOGI(incoming_log_prefix, "Incoming queue started.");
}

void robusto_incoming_init(char *_log_prefix)
{

    incoming_log_prefix = _log_prefix;
    robusto_network_service_init(_log_prefix);
    robusto_incoming_network_init(_log_prefix);
}