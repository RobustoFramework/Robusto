#include <robconfig.h>
#include <inttypes.h>
#include <robusto_pubsub.h>
#include <robusto_peer.h>
#include <robusto_retval.h>

// The subscription callback

typedef struct subscribed_topic subscribed_topic_t;

typedef void(subscription_cb)(subscribed_topic_t * topic, uint8_t * data, uint16_t data_length);


struct subscribed_topic
{
    uint32_t topic_hash;
    char *topic_name;
    robusto_peer_t *peer;
    uint16_t conversation_id;
    subscription_cb * callback;
    subscribed_topic_t *next;
};


void robusto_pubsub_remove_topic(subscribed_topic_t * topic);
subscribed_topic_t *robusto_pubsub_client_get_topic(robusto_peer_t * peer, char * topic_name, subscription_cb * callback);
rob_ret_val_t robusto_pubsub_client_publish(subscribed_topic_t * topic, uint8_t * data, int16_t data_length);
rob_ret_val_t robusto_pubsub_client_start();
rob_ret_val_t robusto_pubsub_client_init(char * _log_prefix);
