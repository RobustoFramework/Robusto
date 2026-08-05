#include "robusto_pubsub.h"
#include "robusto_pubsub_server.h"
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER

#include <robusto_network_service.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_system.h>
#include <robusto_concurrency.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static char * pubsub_log_prefix;
static mutex_ref_t s_pubsub_mutex = NULL;

static void pubsub_on_delete_peer(robusto_peer_t *peer)
{
    if (peer == NULL || peer->relation_id_incoming == 0U) {
        return;
    }

    uint32_t removed = robusto_pubsub_server_unsubscribe_peer_from_all(peer);
    if (removed > 0U) {
        ROB_LOGI(pubsub_log_prefix,
                 "Removed %" PRIu32 " PubSub subscriptions for peer %s before delete",
                 removed,
                 peer->name);
    }
}

static void incoming_callback(robusto_message_t *message);
static void shutdown_callback();

static void log_peer_publish_alloc_failure(const char *topic_name,
                                          const char *peer_name,
                                          uint32_t data_length,
                                          uint32_t message_length,
                                          bool prefer_spiram)
{
#ifdef ESP_PLATFORM
    ROB_LOGE(pubsub_log_prefix,
             "Peer publish alloc failed topic=%s peer=%s data_len=%lu msg_len=%lu prefer_spiram=%u internal_free=%u internal_largest=%u heap_8bit=%u spiram_free=%u",
             topic_name,
             peer_name,
             (unsigned long)data_length,
             (unsigned long)message_length,
             prefer_spiram ? 1U : 0U,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)get_free_mem(),
             (unsigned)get_free_mem_spi());
#else
    ROB_LOGE(pubsub_log_prefix,
             "Peer publish alloc failed topic=%s peer=%s data_len=%lu msg_len=%lu prefer_spiram=%u",
             topic_name,
             peer_name,
             (unsigned long)data_length,
             (unsigned long)message_length,
             prefer_spiram ? 1U : 0U);
#endif
}

static void log_large_publish_snapshot(const char *phase,
                                       const char *topic_name,
                                       uint32_t data_length,
                                       uint32_t publish_count,
                                       uint32_t subscriber_count,
                                       uint32_t fail_count)
{
#ifdef ESP_PLATFORM
    ROB_LOGW(pubsub_log_prefix,
             "Large publish %s topic=%s bytes=%lu count=%lu subs=%lu fail=%lu heap_8bit=%u internal_free=%u internal_largest=%u spiram_free=%u",
             phase,
             topic_name,
             (unsigned long)data_length,
             (unsigned long)publish_count,
             (unsigned long)subscriber_count,
             (unsigned long)fail_count,
             (unsigned)get_free_mem(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)get_free_mem_spi());
#else
    ROB_LOGW(pubsub_log_prefix,
             "Large publish %s topic=%s bytes=%lu count=%lu subs=%lu fail=%lu",
             phase,
             topic_name,
             (unsigned long)data_length,
             (unsigned long)publish_count,
             (unsigned long)subscriber_count,
             (unsigned long)fail_count);
#endif
}

static uint8_t *build_peer_publish_message(uint32_t topic_hash,
                                           uint8_t *data,
                                           uint32_t data_length,
                                           bool prefer_spiram,
                                           uint32_t *message_length_out)
{
    const uint32_t payload_length = data_length + 5U;
    const uint32_t message_length = ROBUSTO_PREFIX_BYTES +
                                    ROBUSTO_CRC_LENGTH +
                                    ROBUSTO_CONTEXT_BYTE_LEN +
                                    sizeof(uint16_t) +
                                    payload_length;
    message_context_t context = {
        .message_type = MSG_MESSAGE,
        .is_service_call = true,
        .is_conversation = false,
        .has_strings = false,
        .has_binary = true,
    };
    uint8_t *message = (payload_length > 500U && prefer_spiram)
                           ? robusto_spi_malloc(message_length)
                           : robusto_malloc(message_length);
    uint16_t service_id = PUBSUB_CLIENT_ID;
    uint8_t *payload;
    uint32_t crc32;

    if (message == NULL) {
        return NULL;
    }

    memset(message, 0, ROBUSTO_PREFIX_BYTES);
    message[ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH] = robusto_encode_message_context(&context);
    memcpy(message + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN,
           &service_id,
           sizeof(service_id));

    payload = message + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH +
              ROBUSTO_CONTEXT_BYTE_LEN + sizeof(service_id);
    payload[0] = PUBSUB_DATA;
    memcpy(payload + 1, &topic_hash, sizeof(topic_hash));
    memcpy(payload + 5, data, data_length);

    crc32 = robusto_crc32(0,
                          message + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH,
                          message_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH);
    memcpy(message + ROBUSTO_PREFIX_BYTES, &crc32, ROBUSTO_CRC_LENGTH);

    *message_length_out = message_length;
    return message;
}

static rob_ret_val_t prepare_peer_publish(robusto_peer_t *peer,
                                          const char *topic_name,
                                          uint32_t data_length,
                                          e_media_type *media_type_out)
{
    e_media_type media_type = robusto_mt_none;
    robusto_media_t *media_info;
    rob_ret_val_t media_rc;

    if (peer == NULL || media_type_out == NULL) {
        return ROB_ERR_INVALID_ARG;
    }

    media_rc = set_suitable_media(peer, (uint16_t)(data_length + 5U), robusto_mt_none, &media_type);
    if (media_rc != ROB_OK || media_type == robusto_mt_none) {
        if (data_length > 500U) {
            ROB_LOGW(pubsub_log_prefix,
                     "Skipping large publish of %s to peer %s: no suitable media available len=%lu",
                     topic_name,
                     peer->name,
                     (unsigned long)data_length);
        }
        return ROB_FAIL;
    }

    if (data_length <= 500U) {
        *media_type_out = media_type;
        return ROB_OK;
    }

    media_info = get_media_info(peer, media_type);
    if (media_info == NULL) {
        return ROB_FAIL;
    }
    if (media_info->state != media_state_working) {
        ROB_LOGW(pubsub_log_prefix,
                 "Skipping large publish of %s to peer %s using %s: state=%u problem=%u len=%lu",
                 topic_name,
                 peer->name,
                 media_type_to_str(media_type),
                 media_info->state,
                 media_info->problem,
                 (unsigned long)data_length);
        return ROB_FAIL;
    }

    *media_type_out = media_type;
    return ROB_OK;
}

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

static bool pubsub_lock(void)
{
    if (s_pubsub_mutex == NULL) {
        return true;
    }
    if (robusto_mutex_take(s_pubsub_mutex, 10000) != ROB_OK) {
        ROB_LOGE(pubsub_log_prefix, "PubSub mutex take failed");
        return false;
    }
    return true;
}

static void pubsub_unlock(void)
{
    if (s_pubsub_mutex == NULL) {
        return;
    }
    if (robusto_mutex_give(s_pubsub_mutex) != ROB_OK) {
        ROB_LOGE(pubsub_log_prefix, "PubSub mutex give failed");
    }
}

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

static pubsub_server_topic_t *find_or_create_topic_locked(char *name) {
    if (!name || name[0] == '\0') {
        return NULL;
    }
    pubsub_server_topic_t * existing_topic = find_topic_by_name(name);
    // If topic doesn't exist, create it
    if (!existing_topic) {
        pubsub_server_topic_t *new_topic = robusto_malloc(sizeof(pubsub_server_topic_t));
        if (!new_topic) {
            return NULL;
        }
        size_t name_len = strlen(name);
        new_topic->name = robusto_malloc(name_len + 1);
        if (!new_topic->name) {
            robusto_free(new_topic);
            return NULL;
        }
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

pubsub_server_topic_t * robusto_pubsub_server_find_or_create_topic(char * name) {
    pubsub_server_topic_t *topic;

    if (!pubsub_lock()) {
        return NULL;
    }
    topic = find_or_create_topic_locked(name);
    pubsub_unlock();
    return topic;
}
/**
 * @brief Find out if a peer already has a subscription to a topic
 * 
 * @param topic 
 * @param peer 
 * @return pubsub_server_subscriber_t* 
 */
pubsub_server_subscriber_t * find_subscription(pubsub_server_topic_t *topic,
                                               robusto_peer_t *peer,
                                               pubsub_server_subscriber_callback *local_cb,
                                               pubsub_server_subscriber_context_callback *local_context_cb,
                                               void *local_context) {

    pubsub_server_subscriber_t * curr_subscriber = topic->first_subscriber;
    while (curr_subscriber) {
        if ((peer && curr_subscriber->peer &&
             curr_subscriber->peer->relation_id_incoming == peer->relation_id_incoming) ||
            (local_cb && !curr_subscriber->peer &&
             curr_subscriber->local_callback == local_cb) ||
            (local_context_cb && !curr_subscriber->peer &&
             curr_subscriber->local_context_callback == local_context_cb &&
             curr_subscriber->local_context == local_context)) {
            return curr_subscriber;
        }
        curr_subscriber = curr_subscriber->next;
    }
    return NULL;
}
uint32_t robusto_pubsub_add_subscriber(pubsub_server_topic_t *topic,
                                      robusto_peer_t *peer,
                                      pubsub_server_subscriber_callback *local_cb,
                                      pubsub_server_subscriber_context_callback *local_context_cb,
                                      void *local_context) {
    bool has_local_callback = local_cb || local_context_cb;
    if (!topic || (!peer && !has_local_callback) || (peer && has_local_callback) ||
        (local_cb && local_context_cb)) {
        return 0;
    }
    pubsub_server_subscriber_t * existing = find_subscription(topic, peer, local_cb,
                                                               local_context_cb,
                                                               local_context);
    if (existing) {
        ROB_LOGW(pubsub_log_prefix, "Subscriber already registered for %s, returning hash: %lu.", topic->name, topic->hash);
        return topic->hash;
    }

    pubsub_server_subscriber_t *new_subscriber = robusto_malloc(sizeof(pubsub_server_subscriber_t));
    if (!new_subscriber) {
        return 0;
    }
    new_subscriber->peer = peer;
    new_subscriber->local_callback = local_cb;
    new_subscriber->local_context_callback = local_context_cb;
    new_subscriber->local_context = local_context;
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

    if (!pubsub_lock()) {
        return 0;
    }
    pubsub_server_topic_t *curr_topic = find_or_create_topic_locked(topic_name);
    if (!curr_topic) {
        pubsub_unlock();
        return 0;
    }
    // Add the subscriber, return hash
    uint32_t hash = robusto_pubsub_add_subscriber(curr_topic, peer, local_cb, NULL, NULL);
    pubsub_unlock();
    return hash;
}

uint32_t robusto_pubsub_server_subscribe_with_context(pubsub_server_subscriber_context_callback *local_cb,
                                                      void *context,
                                                      char *topic_name) {
    if (!pubsub_lock()) {
        return 0;
    }
    pubsub_server_topic_t *curr_topic = find_or_create_topic_locked(topic_name);
    if (!curr_topic || !local_cb) {
        pubsub_unlock();
        return 0;
    }
    uint32_t hash = robusto_pubsub_add_subscriber(curr_topic, NULL, NULL, local_cb, context);
    pubsub_unlock();
    return hash;
}

uint32_t robusto_pubsub_server_unsubscribe(robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb, uint32_t topic) {
    if (!pubsub_lock()) {
        return 0;
    }
    pubsub_server_topic_t *curr_topic = find_topic_by_hash(topic);
    pubsub_server_subscriber_t *previous = NULL;
    pubsub_server_subscriber_t *subscriber;

    if (!curr_topic || (!peer && !local_cb) || (peer && local_cb)) {
        pubsub_unlock();
        return 0;
    }

    subscriber = curr_topic->first_subscriber;
    while (subscriber) {
        bool matches = (peer && subscriber->peer &&
                        subscriber->peer->relation_id_incoming == peer->relation_id_incoming) ||
                       (local_cb && !subscriber->peer &&
                        subscriber->local_callback == local_cb);
        if (matches) {
            if (previous) {
                previous->next = subscriber->next;
            } else {
                curr_topic->first_subscriber = subscriber->next;
            }
            if (curr_topic->last_subscriber == subscriber) {
                curr_topic->last_subscriber = previous;
            }
            curr_topic->subscriber_count--;
            robusto_free(subscriber);
            pubsub_unlock();
            return curr_topic->hash;
        }
        previous = subscriber;
        subscriber = subscriber->next;
    }
    pubsub_unlock();
    return 0;
}

uint32_t robusto_pubsub_server_unsubscribe_with_context(pubsub_server_subscriber_context_callback *local_cb,
                                                        void *context,
                                                        uint32_t topic) {
    if (!pubsub_lock()) {
        return 0;
    }
    pubsub_server_topic_t *curr_topic = find_topic_by_hash(topic);
    pubsub_server_subscriber_t *previous = NULL;
    pubsub_server_subscriber_t *subscriber;

    if (!curr_topic || !local_cb) {
        pubsub_unlock();
        return 0;
    }

    subscriber = curr_topic->first_subscriber;
    while (subscriber) {
        if (!subscriber->peer && subscriber->local_context_callback == local_cb &&
            subscriber->local_context == context) {
            if (previous) {
                previous->next = subscriber->next;
            } else {
                curr_topic->first_subscriber = subscriber->next;
            }
            if (curr_topic->last_subscriber == subscriber) {
                curr_topic->last_subscriber = previous;
            }
            curr_topic->subscriber_count--;
            robusto_free(subscriber);
            pubsub_unlock();
            return curr_topic->hash;
        }
        previous = subscriber;
        subscriber = subscriber->next;
    }
    pubsub_unlock();
    return 0;
}

uint32_t robusto_pubsub_server_unsubscribe_peer_from_all(robusto_peer_t *peer) {
    uint32_t removed = 0;

    if (!peer) {
        return 0;
    }
    if (!pubsub_lock()) {
        return 0;
    }

    pubsub_server_topic_t *curr_topic = first_topic;
    while (curr_topic) {
        pubsub_server_subscriber_t *previous = NULL;
        pubsub_server_subscriber_t *subscriber = curr_topic->first_subscriber;

        while (subscriber) {
            if (subscriber->peer &&
                subscriber->peer->relation_id_incoming == peer->relation_id_incoming) {
                pubsub_server_subscriber_t *next = subscriber->next;

                if (previous) {
                    previous->next = next;
                } else {
                    curr_topic->first_subscriber = next;
                }
                if (curr_topic->last_subscriber == subscriber) {
                    curr_topic->last_subscriber = previous;
                }
                if (curr_topic->subscriber_count > 0U) {
                    curr_topic->subscriber_count--;
                }
                robusto_free(subscriber);
                removed++;
                subscriber = next;
                continue;
            }

            previous = subscriber;
            subscriber = subscriber->next;
        }

        curr_topic = curr_topic->next;
    }

    pubsub_unlock();
    return removed;
}

rob_ret_val_t publish_topic(pubsub_server_topic_t * topic, pubsub_server_subscriber_t *subscriber, uint8_t* data, uint32_t data_length) {
    if (subscriber->local_callback) {
        ROB_LOGD(pubsub_log_prefix, "Publishing %s to callback", topic->name);
        return  subscriber->local_callback(data, data_length);
    } else if (subscriber->local_context_callback) {
        ROB_LOGD(pubsub_log_prefix, "Publishing %s to context callback", topic->name);
        return subscriber->local_context_callback(subscriber->local_context, data, data_length);
    } else if (subscriber->peer) {
        e_media_type media_type = robusto_mt_none;
        bool prefer_spiram = robusto_has_spiram();
        uint32_t message_length = 0U;

        if (prepare_peer_publish(subscriber->peer, topic->name, data_length, &media_type) != ROB_OK) {
            return ROB_FAIL;
        }

        ROB_LOGD(pubsub_log_prefix, "Publishing %s to peer %s.", topic->name, subscriber->peer->name);
        uint8_t *msg = build_peer_publish_message(topic->hash,
                                                  data,
                                                  data_length,
                                                  prefer_spiram,
                                                  &message_length);
        if (!msg) {
            ROB_LOGE(pubsub_log_prefix, "Failed allocating memory to publish %s to peer %s.", topic->name, subscriber->peer->name);
            log_peer_publish_alloc_failure(topic->name,
                                           subscriber->peer->name,
                                           data_length,
                                           message_length,
                                           prefer_spiram);
            return ROB_ERR_OUT_OF_MEMORY;
        }

        rob_ret_val_t pubretval = send_message_raw(subscriber->peer,
                                                   media_type,
                                                   msg,
                                                   message_length,
                                                   NULL,
                                                   true);
        if (pubretval != ROB_OK) {
            robusto_free(msg);
        }
        if (pubretval != ROB_OK) {
            ROB_LOGW(pubsub_log_prefix, "Failed publishing %s to peer %s, retval: %i.", topic->name, subscriber->peer->name, pubretval);
        }
        
        return pubretval;
    } else {
        ROB_LOGE(pubsub_log_prefix, "Internal error: Neither peer nor callback set on one subscription in %s!", topic->name);
        return ROB_FAIL;
    }
}

rob_ret_val_t robusto_pubsub_server_publish(uint32_t topic_hash, uint8_t *data, uint32_t data_length) {

    if (!pubsub_lock()) {
        return ROB_ERR_MUTEX;
    }
    pubsub_server_topic_t * curr_topic = find_topic_by_hash(topic_hash);
    bool large_publish;

    // Do NOT create topic if nonexisting as we are only getting a hash -> we can't. 
    if (!curr_topic) {
        ROB_LOGE(pubsub_log_prefix, "Failed to find the topic %lu.", topic_hash);
        pubsub_unlock();
        return ROB_ERR_INVALID_ID;
    }
    curr_topic->count++;
    large_publish = data_length > 8192U;
    if (large_publish &&
        (curr_topic->count <= 2U || ((uint32_t)curr_topic->count % 16U) == 0U)) {
        log_large_publish_snapshot("begin",
                                   curr_topic->name,
                                   data_length,
                                   curr_topic->count,
                                   curr_topic->subscriber_count,
                                   0U);
    }
    int pub_count = 0;
    int fail_count = 0;
    pubsub_server_subscriber_t *curr_subscriber = curr_topic->first_subscriber;
    while (curr_subscriber) {
        if (publish_topic(curr_topic, curr_subscriber, data, data_length) != ROB_OK) {
            fail_count++;
        }
        pub_count++;
        curr_subscriber = curr_subscriber->next;
    }
    if (fail_count > 0) {
        if (large_publish) {
            log_large_publish_snapshot("end_fail",
                                       curr_topic->name,
                                       data_length,
                                       curr_topic->count,
                                       curr_topic->subscriber_count,
                                       (uint32_t)fail_count);
        }
        ROB_LOGW(pubsub_log_prefix, "Published to the %i subscribers of %s, failed in %i cases.", pub_count, curr_topic->name, fail_count);
    } else if (large_publish &&
               curr_topic->subscriber_count == 0U &&
               (curr_topic->count <= 2U || ((uint32_t)curr_topic->count % 16U) == 0U)) {
        log_large_publish_snapshot("end_zero_subscribers",
                                   curr_topic->name,
                                   data_length,
                                   curr_topic->count,
                                   curr_topic->subscriber_count,
                                   0U);
    } 
    pubsub_unlock();
    return fail_count == 0 ? ROB_OK : ROB_ERR_SEND_SOME_FAIL;
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

    rob_log_bit_mesh(ROB_LOG_DEBUG, pubsub_log_prefix, message->binary_data, message->binary_data_length);
    // Register subscription/topic, answer with topic hash
    if (*message->binary_data == PUBSUB_SUBSCRIBE) {
        uint32_t topic_hash = robusto_pubsub_server_subscribe(message->peer, NULL, (char *)(message->binary_data + 1));
        uint8_t *response = robusto_malloc(5);
        response[0] = PUBSUB_SUBSCRIBE_RESPONSE;
        memcpy(response + 1, &topic_hash, 4);
        ROB_LOGI(pubsub_log_prefix, "Sending a subscription response to %s peer, conv id %u, hash %lu", message->peer->name, message->conversation_id, topic_hash);
        //rob_log_bit_mesh(ROB_LOG_INFO, pubsub_log_prefix, response, 5);
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
        ROB_LOGD(pubsub_log_prefix, "Got a publish from %s peer, publishing %lu bytes.", message->peer->name, message->binary_data_length - 5);
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

    if (s_pubsub_mutex != NULL) {
        robusto_mutex_deinit(s_pubsub_mutex);
        s_pubsub_mutex = NULL;
    }

}

rob_ret_val_t robusto_pubsub_server_start(){

    // Start queue
    robusto_register_network_service(&pubsub_server_service);
    return ROB_OK;
};

rob_ret_val_t robusto_pubsub_server_init(char * _log_prefix){
    pubsub_log_prefix = _log_prefix;
    if (s_pubsub_mutex == NULL) {
        s_pubsub_mutex = robusto_mutex_init();
        if (s_pubsub_mutex == NULL) {
            return ROB_ERR_OUT_OF_MEMORY;
        }
    }
    robusto_register_on_delete_peer(pubsub_on_delete_peer);
    return ROB_OK;
};

#endif