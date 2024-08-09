#include "robusto_pubsub.h"
#include "robusto_pubsub_server.h"
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER

#include <robusto_network_service.h>
#include <robusto_message.h>
#include <string.h>

static char * pubsub_log_prefix;

static void incoming_callback(robusto_message_t *message);
static void shutdown_callback();

network_service_t pubsub_server_service = {
    .incoming_callback = &incoming_callback,
    .service_name = "Pub-sub service",
    .service_id = PUBSUB_SERVER_ID,
    .shutdown_callback = &shutdown_callback,
};

/* Linked list of topics */
pubsub_server_topic_t *first_topic = NULL;
/* Last topic in the list */
pubsub_server_topic_t *last_topic = NULL;

pubsub_server_topic_t * find_topic_by_name(char * topic_name) {

    pubsub_server_topic_t * curr_topic = first_topic;
    while (curr_topic) {
        if (strcmp(curr_topic->name, topic_name) == 0) {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

pubsub_server_topic_t * find_topic_by_hash(uint32_t hash) {

    pubsub_server_topic_t * curr_topic = first_topic;
    while (curr_topic) {
        if (curr_topic->hash == hash) {
            return curr_topic;
        }
        curr_topic = curr_topic->next;
    }
    return NULL;
}

pubsub_server_topic_t * robusto_pubsub_server_find_or_create_topic(char * name) {
    pubsub_server_topic_t * existing_topic = find_topic_by_name(name);
    // If topic doesn't exist, create it
    if (!existing_topic) {
        pubsub_server_topic_t *new_topic = robusto_malloc(sizeof(pubsub_server_topic_t));
        uint8_t name_len = strlen(name);
        new_topic->name = robusto_malloc(name_len + 1);
        strcpy(new_topic->name, name);
        new_topic->count = 0;
        new_topic->subscriber_count = 0;
        new_topic->first_subscriber = NULL;
        new_topic->last_subscriber = NULL;
        new_topic->next = NULL;
        new_topic->hash = robusto_crc32(0, (uint8_t *)name, name_len);
        if (!first_topic) {
            first_topic = new_topic;
        } else {
            last_topic->next = new_topic; 
        }
        last_topic = new_topic;
        ROB_LOGI(pubsub_log_prefix, "Topic %s now added", new_topic->name);
        return new_topic;
    } else {
        return existing_topic;
    }
    
}
/**
 * @brief Find out if a peer already has a subscription to a topic
 * 
 * @param topic 
 * @param peer 
 * @return pubsub_server_subscriber_t* 
 */
pubsub_server_subscriber_t * find_subscription_by_subscriber_hash(pubsub_server_topic_t * topic, robusto_peer_t * peer) {

    pubsub_server_subscriber_t * curr_subscriber = topic->first_subscriber;
    while (curr_subscriber) {

        if ((curr_subscriber->peer) && (curr_subscriber->peer->relation_id_incoming == peer->relation_id_incoming)) {
            return curr_subscriber;
        }
        curr_subscriber = curr_subscriber->next;
    }
    return NULL;
}
uint32_t robusto_pubsub_add_subscriber(pubsub_server_topic_t *topic, robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb) {
    pubsub_server_subscriber_t * existing = find_subscription_by_subscriber_hash(topic, peer);
    // TODO: Handle cases where adding with only callback, they might be double
    if (existing) {
        ROB_LOGW(pubsub_log_prefix, "%s asked to subscribe to %s, but is already a subscriber, returning hash: %lu.", peer->name, topic->name, topic->hash);
        return topic->hash;
    }

    pubsub_server_subscriber_t *new_subscriber = robusto_malloc(sizeof(pubsub_server_subscriber_t));
    new_subscriber->peer = peer;
   
    new_subscriber->local_callback = local_cb;
    new_subscriber->next = NULL;
    if (!topic->first_subscriber) {
        topic->first_subscriber = new_subscriber;
    } else {
        topic->last_subscriber->next = new_subscriber;
    }   
    topic->last_subscriber = new_subscriber; 
    if (peer) {
        ROB_LOGI(pubsub_log_prefix, "Peer %s now subscribes to %s", new_subscriber->peer->name, topic->name);
    } else {
        ROB_LOGI(pubsub_log_prefix, "Local peer callback now subscribes to %s", topic->name);
    }
    
    topic->subscriber_count++;

    return topic->hash;
}


uint32_t robusto_pubsub_server_subscribe(robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb, char * topic_name) {

    pubsub_server_topic_t *curr_topic = robusto_pubsub_server_find_or_create_topic(topic_name);
    // Add the subscriber, return hash
    return robusto_pubsub_add_subscriber(curr_topic, peer, local_cb);
}

uint32_t robusto_pubsub_server_unsubscribe(robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb, uint32_t topic) {
    return ROB_OK;
}

rob_ret_val_t publish_topic(pubsub_server_topic_t * topic, pubsub_server_subscriber_t *subscriber, uint* data, int data_length) {
    if (subscriber->local_callback) {
        ROB_LOGI(pubsub_log_prefix, "Publishing %s to callback", topic->name);
        subscriber->local_callback(data, data_length);
    } else if (subscriber->peer) {
        ROB_LOGI(pubsub_log_prefix, "Publishing %s to peer %s.", topic->name, subscriber->peer->name);
        uint8_t *msg = robusto_malloc(data_length + 5);
        msg[0] = PUBSUB_DATA;
        memcpy(msg + 1, &topic->hash, 4);
        memcpy(msg + 5, data, data_length);
        rob_ret_val_t pubretval = send_message_binary(subscriber->peer, PUBSUB_CLIENT_ID, 0, msg, data_length + 5, NULL);
        if (pubretval != ROB_OK) {
            ROB_LOGW(pubsub_log_prefix, "Failed publishing  %s to peer %s, retval: %i.", topic->name, subscriber->peer->name, pubretval);
        }
        
        robusto_free(msg);
    } else {
        ROB_LOGI(pubsub_log_prefix, "Internal error: Neither peer or callback set on one subscription in %s!", topic->name);
        return ROB_FAIL;
    }
    return ROB_OK;
}

rob_ret_val_t robusto_pubsub_server_publish(uint32_t topic_hash, uint8_t *data, uint16_t data_length) {

    pubsub_server_topic_t * curr_topic = find_topic_by_hash(topic_hash);

    // Do NOT create topic if nonexisting as we are only getting a hash -> we can't. 
    if (!curr_topic) {
        ROB_LOGE(pubsub_log_prefix, "Failed to find the topic %lu.", topic_hash);
        return ROB_ERR_INVALID_ID;
    }
    int pub_count = 0;
    pubsub_server_subscriber_t *curr_subscriber = curr_topic->first_subscriber;
    while (curr_subscriber) {
        publish_topic(curr_topic, curr_subscriber, data, data_length);
        curr_subscriber = curr_subscriber->next;
        pub_count++;
    }
    ROB_LOGD(pubsub_log_prefix, "Published to the %i subscribers of %s.", pub_count, curr_topic->name);
    // If successful, return rob_ok.
    return ROB_OK;
}

#define SEND_LOGGED(prefix, peer, conversation_id, response, length)        \
    do                                                                                                               \
    {                                                                                             \
        if (CONFIG_ROB_LOG_MAXIMUM_LEVEL >= ROB_LOG_WARN)                                                                                  \
        {                                                                                                            \
            rob_ret_val_t retval = send_message_binary(peer, PUBSUB_CLIENT_ID, conversation_id, response, length, NULL); \
            if (retval != ROB_OK) {\
                ROB_LOGW(pubsub_log_prefix, "%s to %s peer, conv id %i", prefix, peer->name, conversation_id);\
            }\
        }                                                                                                       \
        else                                                                                                         \
        {                                                                                                            \
            send_message_binary(peer, PUBSUB_CLIENT_ID, conversation_id, response, length, NULL);    \
        }    \
                                                                                                      \
    } while (0)\


void incoming_callback(robusto_message_t *message) {
    ROB_LOGD(pubsub_log_prefix, "Pubsub incoming from %s.", message->peer->name);
    rob_ret_val_t retval = ROB_FAIL;
    rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, message->binary_data, message->binary_data_length);
    // Register subscription/topic, answer with topic hash
    if (*message->binary_data == PUBSUB_SUBSCRIBE) {
        uint32_t topic_hash = robusto_pubsub_server_subscribe(message->peer, NULL, (char *)(message->binary_data + 1));
        uint8_t *response = robusto_malloc(5);
        response[0] = PUBSUB_SUBSCRIBE_RESPONSE;
        memcpy(response + 1, &topic_hash, 4);
        ROB_LOGI(pubsub_log_prefix, "Sending a subscription response to %s peer, conv id %u, hash %lu", message->peer->name, message->conversation_id, topic_hash);
        rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, response, 5);
        SEND_LOGGED("Failed sending a subscription response to ", message->peer, message->conversation_id, response, 5);
    } else if (*message->binary_data == PUBSUB_GET_TOPIC) {
        pubsub_server_topic_t * topic = robusto_pubsub_server_find_or_create_topic((char *)(message->binary_data + 1));
        uint8_t *response = robusto_malloc(5);
        response[0] = PUBSUB_GET_TOPIC_RESPONSE;
        memcpy(response + 1, &topic->hash, 4);
        ROB_LOGI(pubsub_log_prefix, "Sending a get topic response to %s peer, conv id %u, hash %lu", message->peer->name, message->conversation_id, topic->hash);
        rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, response, 5);
        SEND_LOGGED("Failed sending a get topic response to ", message->peer, message->conversation_id, response, 5);
    } else if (*message->binary_data == PUBSUB_UNSUBSCRIBE) {
        uint32_t topic_hash = robusto_pubsub_server_unsubscribe(message->peer, NULL, *(uint32_t *)(message->binary_data + 1));
        uint8_t *response = robusto_malloc(5);
        response[0] = PUBSUB_UNSUBSCRIBE_RESPONSE;
        memcpy(response + 1, &topic_hash, 4);
        ROB_LOGI(pubsub_log_prefix, "Sending a unsubscription response to %s peer, conv id %u", message->peer->name, message->conversation_id);
        rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, response, 5);
        SEND_LOGGED("Failed sending a unsubscription response to ", message->peer, message->conversation_id, response, 5);
    } else if (*message->binary_data == PUBSUB_PUBLISH) {
        ROB_LOGI(pubsub_log_prefix, "Got a publish from %s peer, publishing %lu bytes.", message->peer->name, message->binary_data_length - 5);
        rob_log_bit_mesh(ROB_LOG_DEBUG, pubsub_log_prefix, message->binary_data + 5, message->binary_data_length - 5);
        // We only respond if it is an invalid topic hash
        if (robusto_pubsub_server_publish( *(uint32_t *)(message->binary_data + 1), message->binary_data + 5, message->binary_data_length - 5) == ROB_ERR_INVALID_ID) {
            uint8_t *response = robusto_malloc(5);
            response[0] = PUBSUB_PUBLISH_UNKNOWN_TOPIC;
            memcpy(response + 1, message->binary_data + 1, 4);
            ROB_LOGW(pubsub_log_prefix, "Sending an unknown topic message to %s peer, conv id %u", message->peer->name, message->conversation_id);
            rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, response, 5);
            SEND_LOGGED("Failed sending an unknown topic message to ", message->peer, message->conversation_id, response, 5);
        }
        
    } else {
        ROB_LOGW(pubsub_log_prefix, "PubSub: Unknown command %hu.", *message->binary_data);
    } 
}


void shutdown_callback() {


}

rob_ret_val_t robusto_pubsub_server_start(){

    // Start queue
    robusto_register_network_service(&pubsub_server_service);
    return ROB_OK;
};

rob_ret_val_t robusto_pubsub_server_init(char * _log_prefix){
    pubsub_log_prefix = _log_prefix;  
    return ROB_OK;
};

#endif