#include "robusto_pubsub_client.h"
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_network_service.h>
#include <robusto_pubsub.h>
#include <robusto_queue.h>
#include <string.h>

static uint16_t pubsub_conversation_id = 1;

static char *pubsub_client_log_prefix;

static void incoming_callback(robusto_message_t *message);
static void shutdown_callback();

static subscribed_topic_t *first_subscribed_topic;
static subscribed_topic_t *last_subscribed_topic;

network_service_t pubsub_client_service = {
    .incoming_callback = &incoming_callback,
    .service_name = "Pub-Sub service",
    .service_id = PUBSUB_CLIENT_ID,
    .shutdown_callback = &shutdown_callback,
};

subscribed_topic_t *find_subscribed_topic_by_conversation_id(int16_t conversation_id)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {
        if (curr_topic->conversation_id == conversation_id)
        {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}


void robusto_pubsub_remove_topic(subscribed_topic_t * topic) {
    if (!topic) {
        return;
    }
    if (topic == first_subscribed_topic) {
        if (topic->next) {
            first_subscribed_topic = topic->next;
            if (!first_subscribed_topic->next) {
                last_subscribed_topic = first_subscribed_topic;
            }
        } else {
            first_subscribed_topic = NULL;
        }

        robusto_free(topic);
        return;
    }

    subscribed_topic_t *last_topic = first_subscribed_topic;
    subscribed_topic_t *curr_topic = first_subscribed_topic->next;
   
    while (curr_topic)
    {
        if (curr_topic == topic)
        {
            last_topic->next = topic->next;
            if (!last_topic->next) {
                last_subscribed_topic = last_topic;
            }

            robusto_free(topic);
            return;
        }
        last_topic = curr_topic;
        curr_topic = curr_topic->next;
    }
    // TODO: We might want to use a standard linked list instead of doing our own.     
}


subscribed_topic_t *find_subscribed_topic_by_topic_hash(int32_t topic_hash)
{
    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {

        if (curr_topic->topic_hash == topic_hash)
        {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

subscribed_topic_t *find_subscribed_topic_by_name(char * topic_name)
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
void incoming_callback(robusto_message_t *message)
{
    ROB_LOGI(pubsub_client_log_prefix, "Got pubsub data from %s peer, first byte %hu", message->peer->name, *message->binary_data);
    rob_log_bit_mesh(ROB_LOG_DEBUG, pubsub_client_log_prefix, message->binary_data, message->binary_data_length);
    if ((*message->binary_data == PUBSUB_SUBSCRIBE_RESPONSE) || 
    (*message->binary_data == PUBSUB_GET_TOPIC_RESPONSE))
    {
        subscribed_topic_t *curr_topic = find_subscribed_topic_by_conversation_id(message->conversation_id);
        if (curr_topic)
        {
            memcpy(&curr_topic->topic_hash, message->binary_data + 1, 4);
            ROB_LOGI(pubsub_client_log_prefix, "Topic hash for %s set to %lu", curr_topic->topic_name, curr_topic->topic_hash);
        }
        else
        {
            ROB_LOGE(pubsub_client_log_prefix, "Could not find matching topic for conversation_id %u!", message->conversation_id);
        }
    }
    else if (*message->binary_data == PUBSUB_DATA)
    {   
        subscribed_topic_t *topic = find_subscribed_topic_by_topic_hash(*(uint32_t *)(message->binary_data + 1));
        
        if (topic)
        {
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
            ROB_LOGE(pubsub_client_log_prefix, "Invalid topic hash %lu!", *(uint32_t *)(message->binary_data +1));
        }
    }
    else
    {
        ROB_LOGE(pubsub_client_log_prefix, "Unhandled pub sub byte %hu!", *message->binary_data);
        rob_log_bit_mesh(ROB_LOG_INFO, pubsub_client_log_prefix, message->binary_data, message->binary_data_length);
    }
}

void shutdown_callback()
{
}

rob_ret_val_t robusto_pubsub_client_publish(subscribed_topic_t *topic, uint8_t *data, int16_t data_length)
{

    if ((topic->peer != NULL) && (topic->peer->state != PEER_UNKNOWN))
    {
        uint8_t *request = robusto_malloc(data_length + 5);
        request[0] = PUBSUB_PUBLISH;
        memcpy(request + 1, &topic->topic_hash, sizeof(topic->topic_hash));
        memcpy(request + 5, data, data_length);
        send_message_binary(topic->peer, PUBSUB_SERVER_ID, 0, request, data_length + 5, NULL);
        return ROB_OK;
    }
    else
    {
        ROB_LOGW(pubsub_client_log_prefix, "Could not send to NMEA gateway, not initiated or peer still unknown.");
        return ROB_FAIL;
    }
}

subscribed_topic_t *_add_topic_and_conv(robusto_peer_t *peer, char *topic_name, subscription_cb *callback)
{

    subscribed_topic_t *new_topic = robusto_malloc(sizeof(subscribed_topic_t));

    new_topic->topic_name = robusto_malloc(strlen(topic_name) + 1);
    strcpy(new_topic->topic_name, topic_name);
    new_topic->next = NULL;
    new_topic->peer = peer;
    new_topic->topic_hash = 0;
    new_topic->callback = callback;
    if (!first_subscribed_topic)
    {
        first_subscribed_topic = new_topic;
    }
    else
    {
       last_subscribed_topic->next = new_topic; 
    }
    last_subscribed_topic = new_topic;
    
    return new_topic;
}

subscribed_topic_t *robusto_pubsub_client_get_topic(robusto_peer_t *peer, char *topic_name, subscription_cb *subscription_callback)
{
    subscribed_topic_t *new_topic = find_subscribed_topic_by_name(topic_name);
    // We want to call the server even if we have the topic locally as it might have crashed.
    if (new_topic) {
        // Update the callback if needed.
        new_topic->callback = subscription_callback;
    } else {
        // The topic didn't exist, add it.
        new_topic = _add_topic_and_conv(peer, topic_name, subscription_callback);
    }
    uint16_t conversation_id = pubsub_conversation_id++;
    new_topic->conversation_id = conversation_id;
    char *msg;
    uint16_t data_length = asprintf(&msg, " %s", topic_name) + 1;
    if (subscription_callback)
    {
        msg[0] = PUBSUB_SUBSCRIBE;
    }
    else
    {
        msg[0] = PUBSUB_GET_TOPIC;
    }

    ROB_LOGE(pubsub_client_log_prefix, "Sending subscription for %s conversation_id %u!", topic_name, new_topic->conversation_id);
    send_message_binary(peer, PUBSUB_SERVER_ID, new_topic->conversation_id, (uint8_t *)msg, data_length, NULL);
    if (!robusto_waitfor_uint32_t_change(&new_topic->topic_hash, 4000))
    {
        ROB_LOGE(pubsub_client_log_prefix, "Pub Sub client: Subscription failed, no response with topic hash for %s.", new_topic->topic_name);
        return NULL;
    }
    else
    {
        return new_topic;
    }
}

rob_ret_val_t robusto_pubsub_client_start()
{
    // Start queue
    robusto_register_network_service(&pubsub_client_service);
    return ROB_OK;
};

rob_ret_val_t robusto_pubsub_client_init(char *_log_prefix)
{
    pubsub_client_log_prefix = _log_prefix;
    return ROB_OK;
};
#endif