#pragma once
#include <robconfig.h>
#include <inttypes.h>
#include <robusto_pubsub.h>
#include <robusto_peer.h>
#include <robusto_retval.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The subscription callback


typedef struct subscribed_topic subscribed_topic_t;
typedef void(subscription_cb)(subscribed_topic_t * topic, uint8_t * data, uint16_t data_length);

typedef enum 
{
    TOPIC_STATE_UNSET, /* The topic in not set */
    TOPIC_STATE_INACTIVE, /* The topic in inactive */
    TOPIC_STATE_ACTIVE, /* The topic is inactive */
    TOPIC_STATE_PROBLEM, /* The topic has some problems */
    TOPIC_STATE_RECOVERING, /* We have recently published data */
    TOPIC_STATE_UNKNOWN, /* The topic is unknown by the server */
    TOPIC_STATE_PUBLISHED, /* We have recently published data */
    TOPIC_STATE_STALE, /* Haven't gotten or published data in stale_time_ms */
    TOPIC_STATE_REMOVING, /* Topic is about to be removed */

} topic_state_t;

typedef void(topic_state_cb)(subscribed_topic_t * topic);

void set_topic_state(subscribed_topic_t *topic, topic_state_t state);

struct subscribed_topic
{   
    /* The name of the topic */
    char *topic_name;
    /* Use for  */
    uint8_t display_offset;
    /* The state of the topic */
    topic_state_t state;
    /* Last time the topic got data */
    uint32_t last_data_time;
    /* The time in milliseconds without updates when a topic is considered stale */
    uint32_t stale_time_ms;
    /* Calculated hash used for referencing the topic */
    uint32_t topic_hash;
    /* The peer */
    robusto_peer_t *peer;
    /* A conversation ID for communication */
    uint16_t conversation_id;
    /* Callback that is called when data arrives */
    subscription_cb * callback;
    /* The next topic of the linked list */
    subscribed_topic_t *next;
};

/**
 * @brief Remove a topic from the client (not from the server)
 * 
 * @param topic 
 */
void robusto_pubsub_remove_topic(subscribed_topic_t * topic);
/**
 * @brief Get a new topic from the server 
 * 
 * @param peer The server peer with the topic
 * @param topic_name The name of the topic
 * @param callback What function to call when data arrives
 * @return subscribed_topic_t* The topic structure
 */
subscribed_topic_t *robusto_pubsub_client_get_topic(robusto_peer_t * peer, char * topic_name, subscription_cb * callback, uint8_t display_offset);
/**
 * @brief Publish data to a topic on the server (create if not existing)
 * 
 * @param topic The name of the topic to puplish data to
 * @param data The data to public
 * @param data_length The length of the data
 * @return rob_ret_val_t Success of publication
 */
rob_ret_val_t robusto_pubsub_client_publish(subscribed_topic_t * topic, uint8_t * data, int16_t data_length);
/**
 * @brief Start receiving data
 * 
 * @return rob_ret_val_t Success of operation
 */
rob_ret_val_t robusto_pubsub_client_start();
/**
 * @brief Run a check of all topics, recover those that have problems
 * 
 */
void robusto_pubsub_check_topics();
/**
 * @brief Initialize client (not start)
 * 
 * @param _log_prefix 
 * @param on_state_change 
 * @return rob_ret_val_t 
 */
rob_ret_val_t robusto_pubsub_client_init(char *_log_prefix, topic_state_cb * _on_state_change);

#ifdef __cplusplus
} /* extern "C" */
#endif