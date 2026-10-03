#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_network_service.h>
#include <robusto_pubsub_client.h>
#include <robusto_system.h>
#include <robusto_time.h>

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed;
static int tests_run;
static uint32_t send_count;
static network_service_t *registered_service;
static uint32_t peer_a_deliveries;
static uint32_t peer_b_deliveries;

#define TEST_ASSERT_TRUE(condition) test_assert_true((condition), #condition, __LINE__)
#define TEST_ASSERT_EQUAL(expected, actual) test_assert_equal((expected), (actual), #actual, __LINE__)

static void test_assert_true(int condition, const char *expression, int line)
{
    ++tests_run;
    if (!condition)
    {
        ++tests_failed;
        fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

static void test_assert_equal(uint32_t expected, uint32_t actual,
                              const char *expression, int line)
{
    ++tests_run;
    if (expected != actual)
    {
        ++tests_failed;
        fprintf(stderr, "FAIL line %d: %s expected %u got %u\n",
                line, expression, expected, actual);
    }
}

void *robusto_malloc(size_t size)
{
    return malloc(size);
}

void robusto_free(void *pointer)
{
    free(pointer);
}

int robusto_asprintf(char **output, const char *format, ...)
{
    va_list arguments;
    va_list copied_arguments;
    int length;

    va_start(arguments, format);
    va_copy(copied_arguments, arguments);
    length = vsnprintf(NULL, 0U, format, copied_arguments);
    va_end(copied_arguments);
    if (length < 0)
    {
        *output = NULL;
        va_end(arguments);
        return length;
    }
    *output = malloc((size_t)length + 1U);
    if (*output == NULL)
    {
        va_end(arguments);
        return -1;
    }
    (void)vsnprintf(*output, (size_t)length + 1U, format, arguments);
    va_end(arguments);
    return length;
}

uint32_t robusto_crc32(uint32_t crc, const uint8_t *buffer, size_t length)
{
    crc = ~crc;
    while (length-- > 0U)
    {
        crc ^= *buffer++;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

unsigned long r_millis(void)
{
    static unsigned long now;
    return ++now;
}

void rob_log_bit_mesh(rob_log_level_t level, const char *tag,
                      const uint8_t *data, int data_length)
{
    (void)level;
    (void)tag;
    (void)data;
    (void)data_length;
}

rob_ret_val_t robusto_register_network_service(network_service_t *service)
{
    registered_service = service;
    return ROB_OK;
}

rob_ret_val_t send_message_binary(robusto_peer_t *peer, uint16_t service_id,
                                  uint16_t conversation_id, uint8_t *binary_data,
                                  uint32_t binary_length, queue_state *state)
{
    (void)peer;
    (void)service_id;
    (void)conversation_id;
    (void)binary_data;
    (void)binary_length;
    (void)state;
    ++send_count;
    return ROB_OK;
}

static void peer_a_callback(subscribed_topic_t *topic, uint8_t *data, uint32_t data_length)
{
    (void)topic;
    (void)data;
    (void)data_length;
    ++peer_a_deliveries;
}

static void peer_b_callback(subscribed_topic_t *topic, uint8_t *data, uint32_t data_length)
{
    (void)topic;
    (void)data;
    (void)data_length;
    ++peer_b_deliveries;
}

static void deliver_message(robusto_peer_t *peer, uint16_t conversation_id,
                            uint8_t command, uint32_t topic_hash)
{
    uint8_t payload[6] = {0};
    robusto_message_t message = {0};

    payload[0] = command;
    memcpy(payload + 1, &topic_hash, sizeof(topic_hash));
    payload[5] = 0x5AU;
    message.peer = peer;
    message.conversation_id = conversation_id;
    message.binary_data = payload;
    message.binary_data_length = sizeof(payload);
    registered_service->incoming_callback(&message);
}

int main(void)
{
    robusto_peer_t peer_a = {0};
    robusto_peer_t peer_b = {0};
    robusto_peer_t peer_without_topic = {0};
    subscribed_topic_t *topic_a;
    subscribed_topic_t *topic_b;

    strcpy(peer_a.name, "peer-a");
    strcpy(peer_b.name, "peer-b");
    strcpy(peer_without_topic.name, "peer-c");
    peer_a.state = PEER_UNKNOWN;
    peer_b.state = PEER_KNOWN_INSECURE;
    peer_without_topic.state = PEER_KNOWN_INSECURE;

    TEST_ASSERT_EQUAL(ROB_OK, robusto_pubsub_client_init("test", NULL));
    TEST_ASSERT_EQUAL(ROB_OK, robusto_pubsub_client_start());
    TEST_ASSERT_TRUE(registered_service != NULL);

    topic_a = robusto_pubsub_client_get_topic(&peer_a, "shared.topic",
                                               peer_a_callback, 0U);
    TEST_ASSERT_TRUE(topic_a != NULL);
    TEST_ASSERT_EQUAL(TOPIC_STATE_WAITING_FOR_PEER, topic_a->state);
    TEST_ASSERT_EQUAL(0U, send_count);

    peer_a.state = PEER_KNOWN_INSECURE;
    robusto_pubsub_client_recover_peer_subscriptions(&peer_a, presentation_reply);
    TEST_ASSERT_EQUAL(1U, send_count);
    TEST_ASSERT_EQUAL(TOPIC_STATE_SUBSCRIBING, topic_a->state);
    deliver_message(&peer_a, topic_a->conversation_id,
                    PUBSUB_SUBSCRIBE_RESPONSE, topic_a->topic_hash);
    TEST_ASSERT_EQUAL(TOPIC_STATE_INACTIVE, topic_a->state);

    topic_b = robusto_pubsub_client_get_topic(&peer_b, "shared.topic",
                                               peer_b_callback, 0U);
    TEST_ASSERT_TRUE(topic_b != NULL);
    TEST_ASSERT_TRUE(topic_b != topic_a);
    TEST_ASSERT_EQUAL(2U, send_count);
    deliver_message(&peer_b, topic_b->conversation_id,
                    PUBSUB_SUBSCRIBE_RESPONSE, topic_b->topic_hash);

    deliver_message(&peer_a, 0U, PUBSUB_DATA, topic_a->topic_hash);
    TEST_ASSERT_EQUAL(1U, peer_a_deliveries);
    TEST_ASSERT_EQUAL(0U, peer_b_deliveries);

    deliver_message(&peer_b, 0U, PUBSUB_DATA, topic_b->topic_hash);
    TEST_ASSERT_EQUAL(1U, peer_a_deliveries);
    TEST_ASSERT_EQUAL(1U, peer_b_deliveries);

    deliver_message(&peer_without_topic, 0U, PUBSUB_DATA, topic_a->topic_hash);
    TEST_ASSERT_EQUAL(1U, peer_a_deliveries);
    TEST_ASSERT_EQUAL(1U, peer_b_deliveries);

    if (tests_failed != 0)
    {
        fprintf(stderr, "PubSub client contract failed: %d of %d assertions\n",
                tests_failed, tests_run);
        return 1;
    }
    printf("PubSub client contract: %d assertions, 0 failures\n", tests_run);
    return 0;
}
