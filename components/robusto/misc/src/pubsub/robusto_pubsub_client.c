#include "robusto_pubsub_client.h"
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_network_service.h>
#include <robusto_pubsub.h>
#include <robusto_queue.h>
#include <robusto_system.h>
#include <string.h>
#include <robusto_time.h>

static uint16_t pubsub_conversation_id = 1;

static char *pubsub_client_log_prefix;

static void incoming_callback(robusto_message_t *message);
static void shutdown_callback();
static rob_ret_val_t request_topic(subscribed_topic_t *topic);

static subscribed_topic_t *first_subscribed_topic = NULL;
static subscribed_topic_t *last_subscribed_topic = NULL;
static bool pubsub_client_started = false;

topic_state_cb *on_state_change_cb;

network_service_t pubsub_client_service = {
    .incoming_callback = &incoming_callback,
    .service_name = "Pub-Sub service",
    .service_id = PUBSUB_CLIENT_ID,
    .shutdown_callback = &shutdown_callback,
};

void set_topic_state(subscribed_topic_t *topic, topic_state_t state)
{
    if (topic->state == state)
    {
        return;
    }
    topic->state = state;
    if (on_state_change_cb)
    {
        on_state_change_cb(topic);
    }
}

static subscribed_topic_t *find_subscribed_topic_by_conversation_id(robusto_peer_t *peer, uint16_t conversation_id)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {
        if (curr_topic->peer == peer && curr_topic->conversation_id == conversation_id)
        {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

void robusto_pubsub_remove_topic(subscribed_topic_t *topic)
{
    if (!topic)
    {
        return;
    }
    if (topic == first_subscribed_topic)
    {
        set_topic_state(topic, TOPIC_STATE_REMOVING);
        if (topic->next)
        {
            first_subscribed_topic = topic->next;
            if (!first_subscribed_topic->next)
            {
                last_subscribed_topic = first_subscribed_topic;
            }
        }
        else
        {
            first_subscribed_topic = NULL;
        }

        robusto_free(topic->topic_name);
        robusto_free(topic);
        return;
    }

    subscribed_topic_t *last_topic = first_subscribed_topic;
    subscribed_topic_t *curr_topic = first_subscribed_topic->next;

    while (curr_topic)
    {
        if (curr_topic == topic)
        {
            set_topic_state(topic, TOPIC_STATE_REMOVING);
            last_topic->next = topic->next;
            if (!last_topic->next)
            {
                last_subscribed_topic = last_topic;
            }

            robusto_free(topic->topic_name);
            robusto_free(topic);
            return;
        }
        last_topic = curr_topic;
        curr_topic = curr_topic->next;
    }
    // TODO: We might want to use a standard linked list instead of doing our own.
}

static subscribed_topic_t *find_subscribed_topic_by_topic_hash(robusto_peer_t *peer, uint32_t topic_hash)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {

        if (curr_topic->peer == peer && curr_topic->topic_hash == topic_hash)
        {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

subscribed_topic_t *find_subscribed_topic_by_name(char *topic_name)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {

        if (strcmp(curr_topic->topic_name, topic_name) == 0)
        {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

static void log_subscribed_topics(const char *reason)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    (void)reason;

    ROB_LOGW(pubsub_client_log_prefix,
             "Subscribed topic dump: %s",
             reason != NULL ? reason : "no reason provided");

    if (curr_topic == NULL)
    {
        ROB_LOGW(pubsub_client_log_prefix, "No subscribed topics registered in client.");
        return;
    }

    while (curr_topic)
    {
        ROB_LOGW(pubsub_client_log_prefix,
                 "Registered topic name=%s hash=%lu conv=%u state=%u peer=%s",
                 curr_topic->topic_name != NULL ? curr_topic->topic_name : "<null>",
                 (unsigned long)curr_topic->topic_hash,
                 curr_topic->conversation_id,
                 curr_topic->state,
                 curr_topic->peer != NULL && curr_topic->peer->name != NULL ? curr_topic->peer->name : "<null>");
        curr_topic = curr_topic->next;
    }
}

void incoming_callback(robusto_message_t *message)
{
    ROB_LOGD(pubsub_client_log_prefix, "Got pubsub data from %s peer, first byte %hu", message->peer->name, *message->binary_data);
    rob_log_bit_mesh(ROB_LOG_DEBUG, pubsub_client_log_prefix, message->binary_data, message->binary_data_length);
    if (*message->binary_data == PUBSUB_PUBLISH_UNKNOWN_TOPIC)
    {
        subscribed_topic_t *curr_topic = find_subscribed_topic_by_topic_hash(message->peer, *(uint32_t *)(message->binary_data + 1));
        if (curr_topic)
        {
            ROB_LOGW(pubsub_client_log_prefix, "Server told us that the topic %s (topic hash %lu) is unknown,", curr_topic->topic_name, curr_topic->topic_hash);
            set_topic_state(curr_topic, TOPIC_STATE_UNKNOWN);
        } else {
            ROB_LOGW(pubsub_client_log_prefix, "Server told us that the %lu topic hash is unknown, it is to us too, newly removed?", *(uint32_t *)(message->binary_data + 1));
        }
        
    } else
    if ((*message->binary_data == PUBSUB_SUBSCRIBE_RESPONSE) ||
        (*message->binary_data == PUBSUB_GET_TOPIC_RESPONSE))
    {
        subscribed_topic_t *curr_topic = find_subscribed_topic_by_conversation_id(message->peer, message->conversation_id);
        if (curr_topic)
        {
            if (curr_topic->topic_hash != 0U && curr_topic->topic_hash != *(uint32_t *)(message->binary_data + 1))
            {
                ROB_LOGW(pubsub_client_log_prefix,
                         "Server returned different hash for %s: local=%lu remote=%lu",
                         curr_topic->topic_name,
                         (unsigned long)curr_topic->topic_hash,
                         (unsigned long)*(uint32_t *)(message->binary_data + 1));
            }
            memcpy(&curr_topic->topic_hash, message->binary_data + 1, 4);
            ROB_LOGI(pubsub_client_log_prefix, "Topic hash for %s set to %lu", curr_topic->topic_name, curr_topic->topic_hash);
            curr_topic->last_data_time = r_millis();
            set_topic_state(curr_topic, TOPIC_STATE_INACTIVE);
        }
        else
        {
            uint32_t hash;
            memcpy(&hash, message->binary_data + 1, 4);
            curr_topic = find_subscribed_topic_by_topic_hash(message->peer, hash);
            if (curr_topic)
            {
                curr_topic->last_data_time = r_millis();
                set_topic_state(curr_topic, TOPIC_STATE_INACTIVE);
            }
            else
            {
                ROB_LOGE(pubsub_client_log_prefix, "Could not find matching topic for conversation_id %u!", message->conversation_id);
            }
        }
    }
    else if (*message->binary_data == PUBSUB_DATA)
    {
        subscribed_topic_t *topic = find_subscribed_topic_by_topic_hash(message->peer, *(uint32_t *)(message->binary_data + 1));

        if (topic)
        {
            topic->last_data_time = r_millis();
            set_topic_state(topic, TOPIC_STATE_ACTIVE);
            if (topic->callback)
            {

                topic->callback(topic, message->binary_data + 5, message->binary_data_length - 5);
            }
            else
            {
                ROB_LOGE(pubsub_client_log_prefix, "No callback defined for topic %s!", topic->topic_name);
            }
        }
        else
        {
            ROB_LOGE(pubsub_client_log_prefix, "Invalid topic hash %lu!", *(uint32_t *)(message->binary_data + 1));
            log_subscribed_topics("incoming pubsub data with unknown topic hash");
        }
    }
    else if (*message->binary_data == PUBSUB_UNSUBSCRIBE_RESPONSE)
    {
        ROB_LOGI(pubsub_client_log_prefix,
                 "Received unsubscribe response for topic hash %lu",
                 (unsigned long)(*(uint32_t *)(message->binary_data + 1)));
    }
    else
    {
        ROB_LOGE(pubsub_client_log_prefix, "Unhandled pub sub byte %hu!", *message->binary_data);
        rob_log_bit_mesh(ROB_LOG_ERROR, pubsub_client_log_prefix, message->binary_data, message->binary_data_length);
    }
}

void shutdown_callback()
{
}

rob_ret_val_t robusto_pubsub_client_unsubscribe(subscribed_topic_t *topic)
{
    uint8_t request[5];
    rob_ret_val_t ret_msg;

    if (topic == NULL || topic->peer == NULL || topic->topic_hash == 0U)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (topic->peer->state < PEER_KNOWN_INSECURE)
    {
        robusto_pubsub_remove_topic(topic);
        return ROB_OK;
    }

    request[0] = PUBSUB_UNSUBSCRIBE;
    memcpy(request + 1, &topic->topic_hash, sizeof(topic->topic_hash));
    ret_msg = send_message_binary(topic->peer,
                                  PUBSUB_SERVER_ID,
                                  pubsub_conversation_id++,
                                  request,
                                  sizeof(request),
                                  NULL);
    if (ret_msg != ROB_OK)
    {
        ROB_LOGE(pubsub_client_log_prefix,
                 "Pub Sub client: Unsubscribe failed for %s.",
                 topic->topic_name);
        set_topic_state(topic, TOPIC_STATE_PROBLEM);
        return ret_msg;
    }

    robusto_pubsub_remove_topic(topic);
    return ROB_OK;
}

rob_ret_val_t robusto_pubsub_client_publish(subscribed_topic_t *topic, uint8_t *data, uint32_t data_length)
{
    if (topic == NULL || topic->peer == NULL || topic->topic_name == NULL ||
        (data_length > 0U && data == NULL))
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (topic->peer->state >= PEER_KNOWN_INSECURE)
    {
        uint8_t *request = robusto_malloc(data_length + 5);
        if (request == NULL)
        {
            set_topic_state(topic, TOPIC_STATE_PROBLEM);
            return ROB_ERR_OUT_OF_MEMORY;
        }
        request[0] = PUBSUB_PUBLISH;
        memcpy(request + 1, &topic->topic_hash, sizeof(topic->topic_hash));
        memcpy(request + 5, data, data_length);
        rob_ret_val_t ret_msg = send_message_binary(topic->peer, PUBSUB_SERVER_ID, 0, request, data_length + 5, NULL);
        robusto_free(request);
        if (ret_msg != ROB_OK) {
            set_topic_state(topic, TOPIC_STATE_PROBLEM);    
        } else {
            set_topic_state(topic, TOPIC_STATE_PUBLISHED);
        }
        
        return ret_msg;
    }
    else
    {
        ROB_LOGE(pubsub_client_log_prefix, "Could not publish %s to %s, peer is not ready.", topic->topic_name, topic->peer->name);
        set_topic_state(topic, TOPIC_STATE_WAITING_FOR_PEER);
        return ROB_ERR_NOT_READY;
    }
}

static subscribed_topic_t *_add_topic_and_conv(robusto_peer_t *peer, const char *topic_name, subscription_cb *callback, uint8_t display_offset)
{

    subscribed_topic_t *new_topic = robusto_malloc(sizeof(subscribed_topic_t));
    if (new_topic == NULL)
    {
        return NULL;
    }

    new_topic->topic_name = robusto_malloc(strlen(topic_name) + 1);
    if (new_topic->topic_name == NULL)
    {
        robusto_free(new_topic);
        return NULL;
    }
    strcpy(new_topic->topic_name, topic_name);
    new_topic->next = NULL;
    new_topic->peer = peer;
    new_topic->topic_hash = robusto_crc32(0, (const uint8_t *)topic_name, strlen(topic_name));
    new_topic->callback = callback;
    new_topic->display_offset = display_offset;
    new_topic->conversation_id = 0;
    new_topic->state = TOPIC_STATE_UNSET;

    // TODO: Odd to not use the linked list macros here? Or anywhere else?
    if (!first_subscribed_topic)
    {
        first_subscribed_topic = new_topic;
    }
    else
    {
        last_subscribed_topic->next = new_topic;
    }
    last_subscribed_topic = new_topic;

    ROB_LOGW(pubsub_client_log_prefix,
             "Added subscribed topic name=%s conv=%u peer=%s callback=%s",
             new_topic->topic_name,
             new_topic->conversation_id,
             new_topic->peer != NULL && new_topic->peer->name != NULL ? new_topic->peer->name : "<null>",
             new_topic->callback != NULL ? "set" : "null");
    log_subscribed_topics("after adding topic to client list");

    return new_topic;
}

static rob_ret_val_t request_topic(subscribed_topic_t *topic)
{
    char *message;
    int formatted_length;
    uint32_t data_length;
    rob_ret_val_t result;

    if (topic == NULL || topic->peer == NULL || topic->topic_name == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (topic->peer->state < PEER_KNOWN_INSECURE)
    {
        set_topic_state(topic, TOPIC_STATE_WAITING_FOR_PEER);
        return ROB_ERR_NOT_READY;
    }
    if (topic->state == TOPIC_STATE_SUBSCRIBING)
    {
        return ROB_OK;
    }

    topic->conversation_id = pubsub_conversation_id++;
    formatted_length = robusto_asprintf(&message, " %s", topic->topic_name);
    if (formatted_length < 1 || message == NULL)
    {
        set_topic_state(topic, TOPIC_STATE_PROBLEM);
        return ROB_ERR_OUT_OF_MEMORY;
    }
    data_length = (uint32_t)formatted_length + 1U;
    if (topic->callback)
    {
        message[0] = PUBSUB_SUBSCRIBE;
    }
    else
    {
        message[0] = PUBSUB_GET_TOPIC;
    }

    ROB_LOGI(pubsub_client_log_prefix,
             "Sending %s for %s to %s conversation_id %u hash %lu",
             topic->callback ? "subscription" : "topic request",
             topic->topic_name,
             topic->peer->name,
             topic->conversation_id,
             (unsigned long)topic->topic_hash);
    result = send_message_binary(topic->peer, PUBSUB_SERVER_ID, topic->conversation_id,
                                 (uint8_t *)message, data_length, NULL);
    robusto_free(message);
    if (result != ROB_OK)
    {
        ROB_LOGE(pubsub_client_log_prefix,
                 "Failed to queue %s request for %s",
                 topic->callback ? "subscription" : "topic",
                 topic->topic_name);
        set_topic_state(topic, TOPIC_STATE_PROBLEM);
        log_subscribed_topics("subscription send failed");
        return result;
    }

    set_topic_state(topic, TOPIC_STATE_SUBSCRIBING);
    return ROB_OK;
}

subscribed_topic_t *robusto_pubsub_client_get_topic(robusto_peer_t *peer, const char *topic_name, subscription_cb *subscription_callback, uint8_t display_offset)
{
    subscribed_topic_t *topic = first_subscribed_topic;

    if (peer == NULL || topic_name == NULL || topic_name[0] == '\0')
    {
        return NULL;
    }
    while (topic != NULL &&
           (topic->peer != peer || strcmp(topic->topic_name, topic_name) != 0))
    {
        topic = topic->next;
    }
    if (topic != NULL)
    {
        topic->callback = subscription_callback;
        topic->display_offset = display_offset;
    }
    else
    {
        topic = _add_topic_and_conv(peer, topic_name, subscription_callback, display_offset);
    }
    if (topic != NULL)
    {
        (void)request_topic(topic);
    }
    return topic;
}

void robusto_pubsub_client_recover_peer_subscriptions(robusto_peer_t *peer, e_presentation_reason reason)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    (void)reason;

    if (peer == NULL)
    {
        return;
    }

    while (curr_topic)
    {
        if (curr_topic->peer == peer && curr_topic->state != TOPIC_STATE_REMOVING)
        {
            ROB_LOGI(pubsub_client_log_prefix,
                     "Activating desired topic %s after peer %s presentation reason %u",
                     curr_topic->topic_name,
                     peer->name,
                     (unsigned)reason);
            if (curr_topic->state == TOPIC_STATE_SUBSCRIBING)
            {
                set_topic_state(curr_topic, TOPIC_STATE_WAITING_FOR_PEER);
            }
            (void)request_topic(curr_topic);
        }
        curr_topic = curr_topic->next;
    }
}

void robusto_pubsub_check_topics()
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {
        if (curr_topic->peer != NULL &&
            curr_topic->peer->state >= PEER_KNOWN_INSECURE &&
            (curr_topic->state == TOPIC_STATE_WAITING_FOR_PEER ||
             curr_topic->state == TOPIC_STATE_PROBLEM ||
             curr_topic->state == TOPIC_STATE_UNKNOWN))
        {
            (void)request_topic(curr_topic);
        }
        curr_topic = curr_topic->next;
    }
}

rob_ret_val_t robusto_pubsub_client_start()
{
    if (pubsub_client_started)
    {
        return ROB_OK;
    }
    rob_ret_val_t result = robusto_register_network_service(&pubsub_client_service);
    if (result == ROB_OK)
    {
        pubsub_client_started = true;
    }
    return result;
};

void robusto_pubsub_client_configure(topic_state_cb *on_state_change)
{
    on_state_change_cb = on_state_change;
}

rob_ret_val_t robusto_pubsub_client_init(char *_log_prefix, topic_state_cb *_on_state_change)
{
    pubsub_client_log_prefix = _log_prefix;
    if (_on_state_change != NULL)
    {
        on_state_change_cb = _on_state_change;
    }
    return ROB_OK;
};
#endif