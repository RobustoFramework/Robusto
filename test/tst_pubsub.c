#include "tst_pubsub.h"
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER
#include <unity.h>


#include <string.h>

#include <robusto_message.h>
#include <robusto_pubsub_server.h>

static uint8_t * pub_data = NULL;
static uint16_t pub_data_length;

void on_data(uint8_t *data, uint16_t data_length) {
    pub_data = data;
    pub_data_length = data_length;
}


void tst_pubsub(void)
{
    pub_data = NULL;
    pubsub_server_topic_t *new_topic = robusto_pubsub_server_find_or_create_topic("test.topic");
    TEST_ASSERT_NOT_NULL_MESSAGE(new_topic, "The new topic is null");
    int32_t topic_hash =  robusto_pubsub_server_subscribe(NULL, &on_data, "test.topic");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(new_topic->hash, topic_hash, "The topic hashes doesn't match");
    char *data = "test";
    robusto_pubsub_server_publish(topic_hash, (uint8_t *)data, 4);
    // Works up to here
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", (char *)pub_data, "The published data doesn't match.");
    
    
}

#endif