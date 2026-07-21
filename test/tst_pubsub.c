#include "tst_pubsub.h"
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER
#include <unity.h>


#include <string.h>

#include <robusto_message.h>
#include <robusto_pubsub_server.h>

static uint8_t * pub_data = NULL;
static uint16_t pub_data_length;
static uint16_t callback_count;
static void *callback_context;

rob_ret_val_t on_data(uint8_t *data, uint32_t data_length) {
    pub_data = data;
    pub_data_length = data_length;
    callback_count++;
    return ROB_OK;
}

rob_ret_val_t on_data_with_context(void *context, uint8_t *data, uint32_t data_length) {
    callback_context = context;
    return on_data(data, data_length);
}


void tst_pubsub(void)
{
    pub_data = NULL;
    callback_count = 0;
    pubsub_server_topic_t *new_topic = robusto_pubsub_server_find_or_create_topic("test.topic");
    TEST_ASSERT_NOT_NULL_MESSAGE(new_topic, "The new topic is null");
    int32_t topic_hash =  robusto_pubsub_server_subscribe(NULL, &on_data, "test.topic");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(new_topic->hash, topic_hash, "The topic hashes doesn't match");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(topic_hash,
                                    robusto_pubsub_server_subscribe(NULL, &on_data, "test.topic"),
                                    "A repeated local subscription must return the same topic hash");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, new_topic->subscriber_count,
                                    "A repeated local subscription must not add a subscriber");
    char *data = "test";
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_OK,
                                  robusto_pubsub_server_publish(topic_hash, (uint8_t *)data, 4),
                                  "Publishing to the local subscriber failed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", (char *)pub_data, "The published data doesn't match.");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, callback_count, "Publish must deliver exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(topic_hash,
                                     robusto_pubsub_server_unsubscribe(NULL, &on_data, topic_hash),
                                     "Unsubscribe must remove the local subscriber");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                     robusto_pubsub_server_unsubscribe(NULL, &on_data, topic_hash),
                                     "Repeated unsubscribe must report no removal");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_OK,
                                  robusto_pubsub_server_publish(topic_hash, (uint8_t *)data, 4),
                                  "Publishing with no subscribers must still succeed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, callback_count,
                                     "No callback may run after unsubscribe");

    uint32_t context_value = 0x12345678;
    callback_context = NULL;
    pub_data = NULL;
    callback_count = 0;
    pubsub_server_topic_t *context_topic = robusto_pubsub_server_find_or_create_topic("test.context");
    TEST_ASSERT_NOT_NULL_MESSAGE(context_topic, "The context topic is null");
    uint32_t context_topic_hash = robusto_pubsub_server_subscribe_with_context(&on_data_with_context,
                                                                               &context_value,
                                                                               "test.context");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(context_topic->hash, context_topic_hash,
                                     "The context topic hashes don't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(context_topic_hash,
                                     robusto_pubsub_server_subscribe_with_context(&on_data_with_context,
                                                                                  &context_value,
                                                                                  "test.context"),
                                     "A repeated context subscription must return the same topic hash");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, context_topic->subscriber_count,
                                    "A repeated context subscription must not add a subscriber");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_OK,
                                  robusto_pubsub_server_publish(context_topic_hash, (uint8_t *)data, 4),
                                  "Publishing to the context subscriber failed");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&context_value, callback_context,
                                  "The subscriber context doesn't match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", (char *)pub_data,
                                     "The context callback data doesn't match");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, callback_count,
                                     "Context publish must deliver exactly once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(context_topic_hash,
                                     robusto_pubsub_server_unsubscribe_with_context(&on_data_with_context,
                                                                                    &context_value,
                                                                                    context_topic_hash),
                                     "Context unsubscribe must remove the local subscriber");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                     robusto_pubsub_server_unsubscribe_with_context(&on_data_with_context,
                                                                                    &context_value,
                                                                                    context_topic_hash),
                                     "Repeated context unsubscribe must report no removal");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_OK,
                                  robusto_pubsub_server_publish(context_topic_hash, (uint8_t *)data, 4),
                                  "Publishing with no context subscribers must still succeed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, callback_count,
                                     "No context callback may run after unsubscribe");

}

#endif