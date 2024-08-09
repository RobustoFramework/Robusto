#include "robusto_pubsub_client.h"
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_network_service.h>
#include <robusto_repeater.h>
#include <robusto_pubsub.h>
#include <robusto_queue.h>
#include <string.h>

static uint16_t pubsub_conversation_id = 1;

static char *pubsub_client_log_prefix;

static void incoming_callback(robusto_message_t *message);
static void shutdown_callback();

static subscribed_topic_t *first_subscribed_topic = NULL;
static subscribed_topic_t *last_subscribed_topic = NULL;

topic_state_cb *on_state_change_cb;

network_service_t pubsub_client_service = {
    .incoming_callback = &incoming_callback,
    .service_name = "Pub-Sub service",
    .service_id = PUBSUB_CLIENT_ID,
    .shutdown_callback = &shutdown_callback,
};

void robusto_pubsub_check_topics();

recurrence_t pubsub_topic_monitor = {
    .recurrence_callback = &robusto_pubsub_check_topics,
    .recurrence_name = "Pubsub topic monitor",
    .shutdown_callback = NULL,
    .skip_count = 40,
    .skips_left = 30,
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
void incoming_callback(robusto_message_t *message)
{
    ROB_LOGI(pubsub_client_log_prefix, "Got pubsub data from %s peer, first byte %hu", message->peer->name, *message->binary_data);
    rob_log_bit_mesh(ROB_LOG_DEBUG, pubsub_client_log_prefix, message->binary_data, message->binary_data_length);
    if (*message->binary_data == PUBSUB_PUBLISH_UNKNOWN_TOPIC)
    {
        subscribed_topic_t *curr_topic = find_subscribed_topic_by_topic_hash(*(uint32_t *)(message->binary_data + 1));
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
        subscribed_topic_t *curr_topic = find_subscribed_topic_by_conversation_id(message->conversation_id);
        if (curr_topic)
        {
            memcpy(&curr_topic->topic_hash, message->binary_data + 1, 4);
            ROB_LOGI(pubsub_client_log_prefix, "Topic hash for %s set to %lu", curr_topic->topic_name, curr_topic->topic_hash);
            curr_topic->last_data_time = r_millis();
            set_topic_state(curr_topic, TOPIC_STATE_INACTIVE);
        }
        else
        {
            uint32_t hash;
            memcpy(&hash, message->binary_data + 1, 4);
            curr_topic = find_subscribed_topic_by_topic_hash(hash);
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
        subscribed_topic_t *topic = find_subscribed_topic_by_topic_hash(*(uint32_t *)(message->binary_data + 1));

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
        rob_ret_val_t ret_msg = send_message_binary(topic->peer, PUBSUB_SERVER_ID, 0, request, data_length + 5, NULL);
        if (ret_msg != ROB_OK) {
            set_topic_state(topic, TOPIC_STATE_PROBLEM);    
        } else {
            set_topic_state(topic, TOPIC_STATE_PUBLISHED);
        }
        
        return ret_msg;
    }
    else
    {
        ROB_LOGE(pubsub_client_log_prefix, "Could not publish %s to %s, not initiated or peer still unknown.", topic->topic_name, topic->peer->name);
        set_topic_state(topic, TOPIC_STATE_PROBLEM);
        return ROB_FAIL;
    }
}

subscribed_topic_t *_add_topic_and_conv(robusto_peer_t *peer, char *topic_name, subscription_cb *callback, uint8_t display_offset)
{

    subscribed_topic_t *new_topic = robusto_malloc(sizeof(subscribed_topic_t));

    new_topic->topic_name = robusto_malloc(strlen(topic_name) + 1);
    strcpy(new_topic->topic_name, topic_name);
    new_topic->next = NULL;
    new_topic->peer = peer;
    new_topic->topic_hash = 0;
    new_topic->callback = callback;
    new_topic->display_offset = display_offset;
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

    return new_topic;
}

subscribed_topic_t *robusto_pubsub_client_get_topic(robusto_peer_t *peer, char *topic_name, subscription_cb *subscription_callback, uint8_t display_offset)
{
    subscribed_topic_t *new_topic = find_subscribed_topic_by_name(topic_name);
    // We want to call the server even if we have the topic locally as it might have crashed.
    if (new_topic)
    {
        // Update the existing callback if needed.
        new_topic->callback = subscription_callback;
        new_topic->display_offset = display_offset;
    }
    else
    {
        // The topic didn't exist, add it.
        new_topic = _add_topic_and_conv(peer, topic_name, subscription_callback, display_offset);
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

    ROB_LOGE(pubsub_client_log_prefix, "Sending subscription for %s conversation_id %u, hash: %lu", topic_name, new_topic->conversation_id, new_topic->topic_hash);
    rob_ret_val_t ret_sub = send_message_binary(peer, PUBSUB_SERVER_ID, new_topic->conversation_id, (uint8_t *)msg, data_length, NULL);
    if (ret_sub != ROB_OK) {
        ROB_LOGE(pubsub_client_log_prefix, "Pub Sub client: Subscription failed, failed to queue or send message %s.", new_topic->topic_name);
        set_topic_state(new_topic, TOPIC_STATE_PROBLEM);
    } else
    if (!new_topic->topic_hash && !robusto_waitfor_uint32_t_change(&new_topic->topic_hash, 1000))
    {
        ROB_LOGE(pubsub_client_log_prefix, "Pub Sub client: Subscription failed, no response with topic hash for %s.", new_topic->topic_name);
        set_topic_state(new_topic, TOPIC_STATE_PROBLEM);
    }
    else
    {
        set_topic_state(new_topic, TOPIC_STATE_INACTIVE);
    }

    return new_topic;
}

void recover_topic(subscribed_topic_t *topic)
{
    robusto_pubsub_client_get_topic(topic->peer, topic->topic_name, topic->callback, topic->display_offset);
    if (topic->state == TOPIC_STATE_PROBLEM && !robusto_waitfor_byte_change(&topic->state, 1000) != ROB_OK)
    {
        ROB_LOGE(pubsub_client_log_prefix, "Failed to recover %s using the %s peer", topic->topic_name, topic->peer->name);
        topic->state = TOPIC_STATE_PROBLEM;
        r_delay(5000);
    }
    else
    {
        // A successful incoming call will set the state to inactive or active elsewhere
        ROB_LOGI(pubsub_client_log_prefix, "Recovered %s using the %s peer!", topic->topic_name, topic->peer->name);
        topic->state = TOPIC_STATE_ACTIVE;
    }
    
    robusto_delete_current_task();
}

void create_topic_recovery_task(subscribed_topic_t *topic)
{
    

    if (topic->peer->state < PEER_KNOWN_INSECURE){
       ROB_LOGW(pubsub_client_log_prefix, "Not creating a topic recovery task for %s topic, peer %s because the peer is not working properly.",
             topic->topic_name, topic->peer->name); 
        return;
    }
    topic->state = TOPIC_STATE_RECOVERING;
    ROB_LOGW(pubsub_client_log_prefix, "Creating a topic recovery task for %s topic, peer %s",
             topic->topic_name, topic->peer->name);
    char *task_name;
    robusto_asprintf(&task_name, "Recovery task task for %s topic, peer %s", topic->topic_name, topic->peer->name);
    if (robusto_create_task(&recover_topic, topic, task_name, NULL, 0) != ROB_OK)
    {
        ROB_LOGE(pubsub_client_log_prefix, "Failed creating a recovery task for %s topic, peer %s", topic->topic_name, topic->peer->name);
    }
    robusto_free(task_name);
}

void robusto_pubsub_check_topics()
{

    subscribed_topic_t *curr_topic = first_subscribed_topic;
    while (curr_topic)
    {
        ROB_LOGW(pubsub_client_log_prefix, "Checking %s, state %hu", curr_topic->topic_name, curr_topic->state);
        if (curr_topic->state == TOPIC_STATE_RECOVERING)
        {
            // Do nothing regardless of state to not disturb any existing recovery processes
        }
        else if ((curr_topic->state == TOPIC_STATE_PROBLEM) || (curr_topic->state == TOPIC_STATE_UNKNOWN))
        {
            // Don't try to recover if we have broader issues with the peer.
            if (curr_topic->peer->problematic_media_types != curr_topic->peer->supported_media_types)
            {
                create_topic_recovery_task(curr_topic);
                r_delay(5000);
            } else {
                ROB_LOGW(pubsub_client_log_prefix, "Will not recover the %s topic now, the %s peer has broader issues.", curr_topic->topic_name, curr_topic->peer->name);
            }
        }

        curr_topic = curr_topic->next;
    }
}

rob_ret_val_t robusto_pubsub_client_start()
{
    // Start queue
    robusto_register_network_service(&pubsub_client_service);
    robusto_register_recurrence(&pubsub_topic_monitor);
    return ROB_OK;
};

rob_ret_val_t robusto_pubsub_client_init(char *_log_prefix, topic_state_cb *_on_state_change)
{
    pubsub_client_log_prefix = _log_prefix;
    on_state_change_cb = _on_state_change;
    return ROB_OK;
};
#endif