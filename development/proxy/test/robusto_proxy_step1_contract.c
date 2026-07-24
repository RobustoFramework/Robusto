#include <robusto_proxy_crc32.h>
#include <robusto_proxy_control.h>
#include <robusto_proxy_client.h>
#include <robusto_proxy_frame.h>
#include <robusto_proxy_inflight.h>
#include <robusto_proxy_pubsub.h>
#include <robusto_proxy_pubsub_adapter.h>
#include <robusto_proxy_pubsub_client.h>
#include <robusto_proxy_service.h>
#include <robusto_proxy_session.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed = 0;
static int tests_run = 0;

#define TEST_ASSERT_TRUE(condition) test_assert_true((condition), #condition, __FILE__, __LINE__)
#define TEST_ASSERT_FALSE(condition) test_assert_true(!(condition), "!(" #condition ")", __FILE__, __LINE__)
#define TEST_ASSERT_EQUAL_U32(expected, actual) test_assert_equal_u32((expected), (actual), #actual, __FILE__, __LINE__)
#define TEST_ASSERT_EQUAL_INT(expected, actual) test_assert_equal_int((expected), (actual), #actual, __FILE__, __LINE__)

static void test_assert_true(int condition, const char *expression, const char *file, int line)
{
    ++tests_run;
    if (!condition)
    {
        ++tests_failed;
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}

static void test_assert_equal_u32(uint32_t expected, uint32_t actual, const char *expression, const char *file, int line)
{
    ++tests_run;
    if (expected != actual)
    {
        ++tests_failed;
        fprintf(stderr, "FAIL %s:%d: %s expected %u got %u\n", file, line, expression, expected, actual);
    }
}

static void test_assert_equal_int(int expected, int actual, const char *expression, const char *file, int line)
{
    ++tests_run;
    if (expected != actual)
    {
        ++tests_failed;
        fprintf(stderr, "FAIL %s:%d: %s expected %d got %d\n", file, line, expression, expected, actual);
    }
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void test_crc32_golden_vector(void)
{
    uint32_t crc = robusto_proxy_crc32_iso_hdlc((const uint8_t *)"123456789", 9U);
    TEST_ASSERT_EQUAL_U32(0xCBF43926U, crc);
}

static void test_header_validation_success_and_failures(void)
{
    robusto_proxy_frame_header_t header;

    robusto_proxy_frame_header_init(
        &header,
        ROBUSTO_PROXY_FLAG_REQUEST,
        ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_HELLO,
        0x10203040U,
        1U,
        0U);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_frame_validate_header(&header));

    header.magic[0] = 0U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_MAGIC, robusto_proxy_frame_validate_header(&header));
    header.magic[0] = 0x52U;

    header.protocol_major = 2U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_VERSION, robusto_proxy_frame_validate_header(&header));
    header.protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;

    header.header_length = 19U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_HEADER_LENGTH, robusto_proxy_frame_validate_header(&header));
    header.header_length = ROBUSTO_PROXY_HEADER_SIZE_BYTES;

    header.flags = ROBUSTO_PROXY_FLAG_REQUEST | ROBUSTO_PROXY_FLAG_RESPONSE;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_FLAGS, robusto_proxy_frame_validate_header(&header));
    header.flags = ROBUSTO_PROXY_FLAG_REQUEST;

    header.reserved = 1U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_RESERVED, robusto_proxy_frame_validate_header(&header));
    header.reserved = 0U;

    header.payload_length = ROBUSTO_PROXY_MAX_PAYLOAD_BYTES + 1U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_LENGTH, robusto_proxy_frame_validate_header(&header));
}

static void test_frame_buffer_validation(void)
{
    uint8_t buffer[ROBUSTO_PROXY_HEADER_SIZE_BYTES + 3U + ROBUSTO_PROXY_CRC_SIZE_BYTES];
    robusto_proxy_frame_header_t *header = (robusto_proxy_frame_header_t *)buffer;
    const uint8_t payload[] = {0xAAU, 0xBBU, 0xCCU};
    uint32_t crc;
    uint32_t expected_crc;

    memset(buffer, 0, sizeof(buffer));
    robusto_proxy_frame_header_init(
        header,
        ROBUSTO_PROXY_FLAG_RESPONSE,
        ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_HEALTH,
        7U,
        9U,
        (uint32_t)sizeof(payload));
    memcpy(buffer + ROBUSTO_PROXY_HEADER_SIZE_BYTES, payload, sizeof(payload));
    expected_crc = robusto_proxy_crc32_iso_hdlc(buffer, sizeof(buffer) - ROBUSTO_PROXY_CRC_SIZE_BYTES);
    write_le32(buffer + sizeof(buffer) - ROBUSTO_PROXY_CRC_SIZE_BYTES, expected_crc);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_frame_validate_buffer(buffer, sizeof(buffer), &crc));
    TEST_ASSERT_EQUAL_U32(expected_crc, crc);

    buffer[sizeof(buffer) - 1U] ^= 0xFFU;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_CRC, robusto_proxy_frame_validate_buffer(buffer, sizeof(buffer), NULL));
}

static void test_frame_encode_round_trip(void)
{
    robusto_proxy_frame_header_t header;
    const uint8_t payload[] = {0x10U, 0x20U, 0x30U};
    uint8_t frame[ROBUSTO_PROXY_HEADER_SIZE_BYTES + sizeof(payload) + ROBUSTO_PROXY_CRC_SIZE_BYTES];
    const robusto_proxy_frame_header_t *decoded_header = (const robusto_proxy_frame_header_t *)frame;
    size_t frame_size = 0U;

    robusto_proxy_frame_header_init(
        &header,
        ROBUSTO_PROXY_FLAG_RESPONSE,
        ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_HEALTH,
        0x10203040U,
        9U,
        (uint32_t)sizeof(payload));

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_frame_encode(frame, sizeof(frame), &header, payload, &frame_size));
    TEST_ASSERT_EQUAL_U32(sizeof(frame), (uint32_t)frame_size);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_frame_validate_buffer(frame, frame_size, NULL));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_FLAG_RESPONSE, decoded_header->flags);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_DOMAIN_CONTROL, decoded_header->domain);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_OPCODE_HEALTH, decoded_header->opcode);
    TEST_ASSERT_EQUAL_U32(0x10203040U, decoded_header->correlation_id);
    TEST_ASSERT_EQUAL_U32(9U, decoded_header->sequence);
    TEST_ASSERT_TRUE(memcmp(frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES, payload, sizeof(payload)) == 0);
}

static void test_slot_empty_detection(void)
{
    uint8_t empty_slot[4] = {0U, 0U, 0x55U, 0x66U};
    uint8_t nonempty_slot[4] = {0x52U, 0x50U, 0U, 0U};

    TEST_ASSERT_TRUE(robusto_proxy_slot_is_empty(empty_slot, sizeof(empty_slot)));
    TEST_ASSERT_FALSE(robusto_proxy_slot_is_empty(nonempty_slot, sizeof(nonempty_slot)));
    TEST_ASSERT_FALSE(robusto_proxy_slot_is_empty(NULL, sizeof(nonempty_slot)));
}

static void test_profile_limits(void)
{
    robusto_proxy_profile_limits_t low = robusto_proxy_profile_limits(ROBUSTO_PROXY_PROFILE_LOW_MEMORY);
    robusto_proxy_profile_limits_t standard = robusto_proxy_profile_limits(ROBUSTO_PROXY_PROFILE_STANDARD);

    TEST_ASSERT_EQUAL_U32(2U, low.max_in_flight);
    TEST_ASSERT_EQUAL_U32(16U, low.max_subscriptions);
    TEST_ASSERT_EQUAL_U32(32U, low.dedupe_entries);
    TEST_ASSERT_EQUAL_U32(4096U, low.request_pool_bytes);

    TEST_ASSERT_EQUAL_U32(8U, standard.max_in_flight);
    TEST_ASSERT_EQUAL_U32(32U, standard.max_subscriptions);
    TEST_ASSERT_EQUAL_U32(64U, standard.dedupe_entries);
    TEST_ASSERT_EQUAL_U32(8192U, standard.response_pool_bytes);
}

static void test_session_seed_and_rollover_behavior(void)
{
    robusto_proxy_session_t session;

    robusto_proxy_session_init(
        &session,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0x1122334455667788ULL,
        0U,
        0xFFFFFFFFU);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_RESET, session.state);
    TEST_ASSERT_EQUAL_U32(1U, robusto_proxy_session_take_correlation_id(&session));
    TEST_ASSERT_EQUAL_U32(2U, robusto_proxy_session_take_correlation_id(&session));
    TEST_ASSERT_EQUAL_U32(0xFFFFFFFFU, robusto_proxy_session_take_sequence(&session));
    TEST_ASSERT_EQUAL_U32(1U, robusto_proxy_session_take_sequence(&session));
}

static void test_hello_response_clamping_and_rejection(void)
{
    robusto_proxy_session_t session;
    robusto_proxy_hello_response_t response;

    robusto_proxy_session_init(&session, ROBUSTO_PROXY_PROFILE_LOW_MEMORY, 1U, 1U, 1U);
    memset(&response, 0, sizeof(response));
    response.proxy_boot_id = 0xABCDEFULL;
    response.selected_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    response.selected_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
    response.selected_max_payload = 4096U;
    response.selected_max_in_flight = 2U;
    response.dedupe_entries = 32U;
    response.dedupe_window_ms = 10000U;
    response.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;

    TEST_ASSERT_TRUE(robusto_proxy_session_apply_hello_response(&session, &response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, session.state);
    TEST_ASSERT_EQUAL_U32(2U, session.negotiated_limits.max_in_flight);
    TEST_ASSERT_EQUAL_U32(4096U, session.negotiated_limits.request_pool_bytes);

    robusto_proxy_session_init(&session, ROBUSTO_PROXY_PROFILE_LOW_MEMORY, 1U, 1U, 1U);
    response.selected_max_payload = 4097U;
    TEST_ASSERT_FALSE(robusto_proxy_session_apply_hello_response(&session, &response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_INCOMPATIBLE, session.state);

    robusto_proxy_session_init(&session, ROBUSTO_PROXY_PROFILE_LOW_MEMORY, 1U, 1U, 1U);
    response.selected_max_payload = 4096U;
    response.selected_protocol_major = 99U;
    TEST_ASSERT_FALSE(robusto_proxy_session_apply_hello_response(&session, &response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_INCOMPATIBLE, session.state);
}

static void test_control_payload_round_trip(void)
{
    robusto_proxy_hello_request_t hello_request;
    robusto_proxy_hello_request_t hello_request_copy;
    robusto_proxy_hello_response_t hello_response;
    robusto_proxy_hello_response_t hello_response_copy;
    robusto_proxy_health_response_t health_response;
    robusto_proxy_health_response_t health_response_copy;
    uint8_t hello_request_bytes[ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES];
    uint8_t hello_response_bytes[ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES];
    uint8_t health_response_bytes[ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES];

    memset(&hello_request, 0, sizeof(hello_request));
    hello_request.controller_boot_id = 0x1122334455667788ULL;
    hello_request.min_protocol_major = 1U;
    hello_request.max_protocol_major = 1U;
    hello_request.min_protocol_minor = 0U;
    hello_request.max_protocol_minor = 0U;
    hello_request.max_payload = 4096U;
    hello_request.max_in_flight = 2U;
    hello_request.required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_hello_request(hello_request_bytes, sizeof(hello_request_bytes), &hello_request));
    TEST_ASSERT_EQUAL_U32(0x88U, hello_request_bytes[0]);
    TEST_ASSERT_EQUAL_U32(0x11U, hello_request_bytes[7]);
    TEST_ASSERT_EQUAL_U32(0x00U, hello_request_bytes[18]);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_hello_request(hello_request_bytes, sizeof(hello_request_bytes), &hello_request_copy));
    TEST_ASSERT_EQUAL_U32(hello_request.max_payload, hello_request_copy.max_payload);
    TEST_ASSERT_EQUAL_U32(hello_request.max_in_flight, hello_request_copy.max_in_flight);
    TEST_ASSERT_TRUE(hello_request.required_features == hello_request_copy.required_features);

    memset(&hello_response, 0, sizeof(hello_response));
    hello_response.proxy_boot_id = 0x8877665544332211ULL;
    hello_response.selected_protocol_major = 1U;
    hello_response.selected_protocol_minor = 0U;
    hello_response.selected_max_payload = 4096U;
    hello_response.selected_max_in_flight = 2U;
    hello_response.dedupe_entries = 32U;
    hello_response.dedupe_window_ms = 10000U;
    hello_response.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    hello_response.proxy_uptime_ms = 1234U;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_hello_response(hello_response_bytes, sizeof(hello_response_bytes), &hello_response));
    TEST_ASSERT_EQUAL_U32(0x11U, hello_response_bytes[0]);
    TEST_ASSERT_EQUAL_U32(0x88U, hello_response_bytes[7]);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_hello_response(hello_response_bytes, sizeof(hello_response_bytes), &hello_response_copy));
    TEST_ASSERT_EQUAL_U32(hello_response.selected_max_payload, hello_response_copy.selected_max_payload);
    TEST_ASSERT_EQUAL_U32(hello_response.selected_max_in_flight, hello_response_copy.selected_max_in_flight);
    TEST_ASSERT_TRUE(hello_response.enabled_features == hello_response_copy.enabled_features);

    memset(&health_response, 0, sizeof(health_response));
    health_response.proxy_boot_id = 0xCAFEBABEULL;
    health_response.uptime_ms = 5000U;
    health_response.service_state = 1U;
    health_response.requests = 10U;
    health_response.events = 20U;
    health_response.errors = 1U;
    health_response.timeouts = 2U;
    health_response.rx_crc_errors = 3U;
    health_response.queue_high_water = 4U;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_health_response(health_response_bytes, sizeof(health_response_bytes), &health_response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_health_response(health_response_bytes, sizeof(health_response_bytes), &health_response_copy));
    TEST_ASSERT_EQUAL_U32(health_response.uptime_ms, health_response_copy.uptime_ms);
    TEST_ASSERT_EQUAL_U32(health_response.requests, health_response_copy.requests);
    TEST_ASSERT_EQUAL_U32(health_response.queue_high_water, health_response_copy.queue_high_water);
}

static void test_control_payload_rejects_short_buffers(void)
{
    robusto_proxy_hello_request_t hello_request;
    robusto_proxy_hello_response_t hello_response;
    robusto_proxy_health_response_t health_response;
    uint8_t short_buffer[8] = {0U};

    memset(&hello_request, 0, sizeof(hello_request));
    memset(&hello_response, 0, sizeof(hello_response));
    memset(&health_response, 0, sizeof(health_response));

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT, robusto_proxy_encode_hello_request(short_buffer, sizeof(short_buffer), &hello_request));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT, robusto_proxy_decode_hello_response(short_buffer, sizeof(short_buffer), &hello_response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT, robusto_proxy_decode_health_response(short_buffer, sizeof(short_buffer), &health_response));
}

static void test_response_prefix_round_trip_and_validation(void)
{
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_response_prefix_t prefix_copy;
    uint8_t bytes[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + 4U] = {0U};

    memset(&prefix, 0, sizeof(prefix));
    prefix.status = ROBUSTO_PROXY_STATUS_BUSY;
    prefix.result_flags = ROBUSTO_PROXY_RESULT_FLAG_RETRYABLE;
    prefix.retry_after_ms = 100U;
    prefix.detail_length = 4U;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_response_prefix(bytes, sizeof(bytes), &prefix));
    TEST_ASSERT_EQUAL_U32(0x05U, bytes[0]);
    TEST_ASSERT_EQUAL_U32(0x01U, bytes[2]);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_response_prefix(bytes, sizeof(bytes), &prefix_copy));
    TEST_ASSERT_EQUAL_U32(prefix.status, prefix_copy.status);
    TEST_ASSERT_EQUAL_U32(prefix.result_flags, prefix_copy.result_flags);
    TEST_ASSERT_EQUAL_U32(prefix.retry_after_ms, prefix_copy.retry_after_ms);
    TEST_ASSERT_EQUAL_U32(prefix.detail_length, prefix_copy.detail_length);
    TEST_ASSERT_FALSE(robusto_proxy_response_prefix_is_success(&prefix_copy));

    bytes[10] = 1U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_RESERVED, robusto_proxy_decode_response_prefix(bytes, sizeof(bytes), &prefix_copy));
    bytes[10] = 0U;

    bytes[2] = 0x02U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_RESERVED, robusto_proxy_decode_response_prefix(bytes, sizeof(bytes), &prefix_copy));
    bytes[2] = 0x01U;

    bytes[8] = 129U;
    bytes[9] = 0U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_LENGTH, robusto_proxy_decode_response_prefix(bytes, sizeof(bytes), &prefix_copy));
}

static void test_prefixed_control_response_messages(void)
{
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_response_prefix_t decoded_prefix;
    robusto_proxy_hello_response_t hello_response;
    robusto_proxy_hello_response_t decoded_hello;
    robusto_proxy_capability_response_t capability_response;
    robusto_proxy_capability_response_t decoded_capability;
    robusto_proxy_health_response_t health_response;
    robusto_proxy_health_response_t decoded_health;
    uint8_t hello_message[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES];
    uint8_t capability_message[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES];
    uint8_t health_message[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES];
    uint8_t error_message[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + 3U] = {0U};

    memset(&prefix, 0, sizeof(prefix));
    prefix.status = ROBUSTO_PROXY_STATUS_OK;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_response_prefix(hello_message, sizeof(hello_message), &prefix));

    memset(&hello_response, 0, sizeof(hello_response));
    hello_response.proxy_boot_id = 0x1122ULL;
    hello_response.selected_protocol_major = 1U;
    hello_response.selected_max_payload = 4096U;
    hello_response.selected_max_in_flight = 2U;
    hello_response.dedupe_entries = 32U;
    hello_response.dedupe_window_ms = 10000U;
    hello_response.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_hello_response(hello_message + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES, &hello_response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_hello_response_message(hello_message, sizeof(hello_message), &decoded_prefix, &decoded_hello));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&decoded_prefix));
    TEST_ASSERT_EQUAL_U32(hello_response.selected_max_in_flight, decoded_hello.selected_max_in_flight);

    memset(&capability_response, 0, sizeof(capability_response));
    capability_response.proxy_boot_id = 0x3344ULL;
    capability_response.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    capability_response.pubsub_major = 1U;
    capability_response.selected_max_payload = 4096U;
    capability_response.selected_max_in_flight = 2U;
    capability_response.max_subscriptions = 16U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_response_prefix(capability_message, sizeof(capability_message), &prefix));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_capability_response(capability_message + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES, &capability_response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_capability_response_message(capability_message, sizeof(capability_message), &decoded_prefix, &decoded_capability));
    TEST_ASSERT_EQUAL_U32(capability_response.max_subscriptions, decoded_capability.max_subscriptions);
    TEST_ASSERT_TRUE(decoded_capability.enabled_features == capability_response.enabled_features);

    memset(&health_response, 0, sizeof(health_response));
    health_response.proxy_boot_id = 0x5566ULL;
    health_response.service_state = 1U;
    health_response.requests = 21U;
    health_response.queue_high_water = 7U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_response_prefix(health_message, sizeof(health_message), &prefix));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_health_response(health_message + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES, &health_response));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_health_response_message(health_message, sizeof(health_message), &decoded_prefix, &decoded_health));
    TEST_ASSERT_EQUAL_U32(health_response.requests, decoded_health.requests);
    TEST_ASSERT_EQUAL_U32(health_response.queue_high_water, decoded_health.queue_high_water);

    memset(&prefix, 0, sizeof(prefix));
    prefix.status = ROBUSTO_PROXY_STATUS_BUSY;
    prefix.result_flags = ROBUSTO_PROXY_RESULT_FLAG_RETRYABLE;
    prefix.retry_after_ms = 50U;
    prefix.detail_length = 3U;
    memcpy(error_message + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, "busy", 3U);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_response_prefix(error_message, sizeof(error_message), &prefix));
    memset(&decoded_hello, 0xA5, sizeof(decoded_hello));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_hello_response_message(error_message, sizeof(error_message), &decoded_prefix, &decoded_hello));
    TEST_ASSERT_FALSE(robusto_proxy_response_prefix_is_success(&decoded_prefix));
    TEST_ASSERT_EQUAL_U32(0U, decoded_hello.selected_max_payload);
    TEST_ASSERT_EQUAL_U32(50U, decoded_prefix.retry_after_ms);
}

static void test_inflight_admission_lookup_completion_and_invalidation(void)
{
    robusto_proxy_inflight_table_t table;
    robusto_proxy_inflight_entry_t *entry;

    robusto_proxy_inflight_init(&table, 2U);
    TEST_ASSERT_EQUAL_U32(2U, table.capacity);
    TEST_ASSERT_EQUAL_U32(0U, table.active_count);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HELLO,
            1U,
            10U,
            100U,
            2000U,
            0xAAAAULL));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            2U,
            11U,
            110U,
            500U,
            0xAAAAULL));
    TEST_ASSERT_EQUAL_U32(2U, table.active_count);

    entry = robusto_proxy_inflight_find(&table, 2U);
    TEST_ASSERT_TRUE(entry != NULL);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_OPCODE_HEALTH, entry->opcode);
    TEST_ASSERT_EQUAL_U32(500U, entry->timeout_ms);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_BAD_LENGTH,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY,
            3U,
            12U,
            120U,
            500U,
            0xAAAAULL));

    TEST_ASSERT_TRUE(robusto_proxy_inflight_complete(&table, 1U));
    TEST_ASSERT_EQUAL_U32(1U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 1U) == NULL);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY,
            3U,
            12U,
            120U,
            500U,
            0xBBBBULL));
    TEST_ASSERT_EQUAL_U32(2U, table.active_count);

    TEST_ASSERT_EQUAL_U32(1U, robusto_proxy_inflight_invalidate_peer(&table, 0xBBBBULL));
    TEST_ASSERT_EQUAL_U32(1U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 3U) == NULL);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 2U) != NULL);
}

static void test_inflight_rejects_invalid_arguments_and_duplicate_correlation(void)
{
    robusto_proxy_inflight_table_t table;

    robusto_proxy_inflight_init(&table, 99U);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS, table.capacity);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HELLO,
            0U,
            1U,
            0U,
            1000U,
            1U));

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HELLO,
            1U,
            1U,
            0U,
            0U,
            1U));

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HELLO,
            7U,
            1U,
            0U,
            1000U,
            1U));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_BAD_RESERVED,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            7U,
            2U,
            10U,
            500U,
            1U));
    TEST_ASSERT_FALSE(robusto_proxy_inflight_complete(&table, 999U));
}

static void test_inflight_expiry_boundaries(void)
{
    robusto_proxy_inflight_table_t table;

    robusto_proxy_inflight_init(&table, 2U);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HELLO,
            10U,
            1U,
            100U,
            500U,
            1U));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            11U,
            2U,
            100U,
            1000U,
            1U));

    TEST_ASSERT_EQUAL_U32(2U, table.active_count);
    TEST_ASSERT_EQUAL_U32(0U, robusto_proxy_inflight_expire(&table, 599U));
    TEST_ASSERT_EQUAL_U32(2U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 10U) != NULL);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 11U) != NULL);

    TEST_ASSERT_EQUAL_U32(1U, robusto_proxy_inflight_expire(&table, 600U));
    TEST_ASSERT_EQUAL_U32(1U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 10U) == NULL);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 11U) != NULL);

    TEST_ASSERT_EQUAL_U32(0U, robusto_proxy_inflight_expire(&table, 1099U));
    TEST_ASSERT_EQUAL_U32(1U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 11U) != NULL);

    TEST_ASSERT_EQUAL_U32(1U, robusto_proxy_inflight_expire(&table, 1100U));
    TEST_ASSERT_EQUAL_U32(0U, table.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&table, 11U) == NULL);
}

static void test_session_degrades_on_timeout_expiry_signal(void)
{
    robusto_proxy_inflight_table_t table;
    robusto_proxy_session_t session;
    robusto_proxy_health_response_t health;
    uint16_t expired;

    robusto_proxy_inflight_init(&table, 1U);
    robusto_proxy_session_init(&session, ROBUSTO_PROXY_PROFILE_LOW_MEMORY, 1U, 1U, 1U);
    session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &table,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            77U,
            9U,
            1000U,
            50U,
            1U));

    expired = robusto_proxy_inflight_expire(&table, 1049U);
    TEST_ASSERT_EQUAL_U32(0U, expired);
    TEST_ASSERT_FALSE(robusto_proxy_session_note_expired_requests(&session, expired));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, session.state);
    TEST_ASSERT_EQUAL_U32(0U, session.timeout_events);

    expired = robusto_proxy_inflight_expire(&table, 1050U);
    TEST_ASSERT_EQUAL_U32(1U, expired);
    TEST_ASSERT_TRUE(robusto_proxy_session_note_expired_requests(&session, expired));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_DEGRADED, session.state);
    TEST_ASSERT_EQUAL_U32(1U, session.timeout_events);

    memset(&health, 0, sizeof(health));
    TEST_ASSERT_TRUE(robusto_proxy_session_apply_health_metrics(&session, &health));
    TEST_ASSERT_EQUAL_U32(2U, health.service_state);
    TEST_ASSERT_EQUAL_U32(1U, health.timeouts);
}

static void test_service_tick_and_health_simulation_timeout_path(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_health_response_t health;
    uint16_t expired;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0xABCDEFULL,
        1U,
        1U,
        2U,
        1000U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &service.inflight,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            500U,
            1U,
            1000U,
            25U,
            0x1111ULL));

    expired = robusto_proxy_service_tick(&service, 1024U);
    TEST_ASSERT_EQUAL_U32(0U, expired);
    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 1024U, &health));
    TEST_ASSERT_EQUAL_U32(1U, health.service_state);
    TEST_ASSERT_EQUAL_U32(0U, health.timeouts);
    TEST_ASSERT_EQUAL_U32(24U, health.uptime_ms);

    expired = robusto_proxy_service_tick(&service, 1025U);
    TEST_ASSERT_EQUAL_U32(1U, expired);
    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 1025U, &health));
    TEST_ASSERT_EQUAL_U32(2U, health.service_state);
    TEST_ASSERT_EQUAL_U32(1U, health.timeouts);
    TEST_ASSERT_EQUAL_U32(25U, health.uptime_ms);
}

static void test_service_health_happy_path_copies_counters(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_health_response_t health;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_STANDARD,
        0x123456789ABCDEF0ULL,
        10U,
        20U,
        8U,
        5000U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.requests = 41U;
    service.events = 17U;
    service.errors = 2U;
    service.rx_crc_errors = 3U;
    service.queue_high_water = 9U;

    TEST_ASSERT_EQUAL_U32(0U, robusto_proxy_service_tick(&service, 5001U));
    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 5050U, &health));
    TEST_ASSERT_EQUAL_U32(0x9ABCDEF0U, (uint32_t)health.proxy_boot_id);
    TEST_ASSERT_EQUAL_U32(50U, health.uptime_ms);
    TEST_ASSERT_EQUAL_U32(1U, health.service_state);
    TEST_ASSERT_EQUAL_U32(41U, health.requests);
    TEST_ASSERT_EQUAL_U32(17U, health.events);
    TEST_ASSERT_EQUAL_U32(2U, health.errors);
    TEST_ASSERT_EQUAL_U32(0U, health.timeouts);
    TEST_ASSERT_EQUAL_U32(3U, health.rx_crc_errors);
    TEST_ASSERT_EQUAL_U32(9U, health.queue_high_water);
}

static void test_service_wraparound_uptime_and_timeout_expiry(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_health_response_t health;
    uint16_t expired;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0x55ULL,
        1U,
        1U,
        2U,
        0xFFFFFFF0U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &service.inflight,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            900U,
            1U,
            0xFFFFFFF0U,
            20U,
            0x2222ULL));

    expired = robusto_proxy_service_tick(&service, 0x00000003U);
    TEST_ASSERT_EQUAL_U32(0U, expired);
    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 0x00000003U, &health));
    TEST_ASSERT_EQUAL_U32(19U, health.uptime_ms);
    TEST_ASSERT_EQUAL_U32(1U, health.service_state);
    TEST_ASSERT_EQUAL_U32(0U, health.timeouts);

    expired = robusto_proxy_service_tick(&service, 0x00000004U);
    TEST_ASSERT_EQUAL_U32(1U, expired);
    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 0x00000004U, &health));
    TEST_ASSERT_EQUAL_U32(20U, health.uptime_ms);
    TEST_ASSERT_EQUAL_U32(2U, health.service_state);
    TEST_ASSERT_EQUAL_U32(1U, health.timeouts);
}

static void test_service_mixed_inflight_expiry_only_expires_due_entries(void)
{
    robusto_proxy_service_t service;
    uint16_t expired;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_STANDARD,
        0x77ULL,
        1U,
        1U,
        4U,
        1000U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &service.inflight,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_HEALTH,
            1001U,
            1U,
            1000U,
            50U,
            1U));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_inflight_begin(
            &service.inflight,
            ROBUSTO_PROXY_DOMAIN_CONTROL,
            ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY,
            1002U,
            2U,
            1000U,
            120U,
            1U));
    TEST_ASSERT_EQUAL_U32(2U, service.inflight.active_count);

    expired = robusto_proxy_service_tick(&service, 1050U);
    TEST_ASSERT_EQUAL_U32(1U, expired);
    TEST_ASSERT_EQUAL_U32(1U, service.inflight.active_count);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&service.inflight, 1001U) == NULL);
    TEST_ASSERT_TRUE(robusto_proxy_inflight_find(&service.inflight, 1002U) != NULL);
    TEST_ASSERT_EQUAL_U32(1U, service.session.timeout_events);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_DEGRADED, service.session.state);
}

static void test_service_health_wire_round_trip_from_builder(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_health_response_t health;
    robusto_proxy_health_response_t decoded;
    uint8_t wire[ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES];

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_STANDARD,
        0x9988776655443322ULL,
        5U,
        6U,
        4U,
        2000U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.requests = 9U;
    service.events = 4U;
    service.errors = 1U;
    service.rx_crc_errors = 2U;
    service.queue_high_water = 3U;

    TEST_ASSERT_TRUE(robusto_proxy_service_build_health_response(&service, 2075U, &health));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_encode_health_response(wire, sizeof(wire), &health));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_decode_health_response(wire, sizeof(wire), &decoded));

    TEST_ASSERT_EQUAL_U32((uint32_t)health.proxy_boot_id, (uint32_t)decoded.proxy_boot_id);
    TEST_ASSERT_EQUAL_U32(health.uptime_ms, decoded.uptime_ms);
    TEST_ASSERT_EQUAL_U32(health.service_state, decoded.service_state);
    TEST_ASSERT_EQUAL_U32(health.requests, decoded.requests);
    TEST_ASSERT_EQUAL_U32(health.events, decoded.events);
    TEST_ASSERT_EQUAL_U32(health.errors, decoded.errors);
    TEST_ASSERT_EQUAL_U32(health.timeouts, decoded.timeouts);
    TEST_ASSERT_EQUAL_U32(health.rx_crc_errors, decoded.rx_crc_errors);
    TEST_ASSERT_EQUAL_U32(health.queue_high_water, decoded.queue_high_water);
}

static void test_service_control_health_request_handler_happy_path(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_health_response_t health;
    uint8_t response[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES];
    size_t response_size = 0U;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_STANDARD,
        0x1111222233334444ULL,
        1U,
        1U,
        4U,
        700U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.events = 8U;
    service.queue_high_water = 2U;

    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_HEALTH,
        NULL,
        0U,
        725U,
        response,
        sizeof(response),
        &response_size));
    TEST_ASSERT_EQUAL_U32(sizeof(response), (uint32_t)response_size);
    TEST_ASSERT_EQUAL_U32(1U, service.requests);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_health_response_message(response, response_size, &prefix, &health));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_EQUAL_U32(25U, health.uptime_ms);
    TEST_ASSERT_EQUAL_U32(1U, health.service_state);
    TEST_ASSERT_EQUAL_U32(1U, health.requests);
    TEST_ASSERT_EQUAL_U32(8U, health.events);
    TEST_ASSERT_EQUAL_U32(2U, health.queue_high_water);
}

static void test_service_control_capability_query_paths(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_capability_response_t capability;
    uint8_t response[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES];
    size_t response_size = 0U;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0x1122334455667788ULL,
        1U,
        1U,
        2U,
        0U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.session.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    service.session.negotiated_limits.max_in_flight = 2U;
    service.session.negotiated_limits.max_subscriptions = 16U;
    service.session.negotiated_limits.request_pool_bytes = 4096U;

    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY,
        NULL,
        0U,
        10U,
        response,
        sizeof(response),
        &response_size));
    TEST_ASSERT_EQUAL_U32(sizeof(response), (uint32_t)response_size);
    TEST_ASSERT_EQUAL_U32(1U, service.requests);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_capability_response_message(response, response_size, &prefix, &capability));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_TRUE(capability.proxy_boot_id == service.session.local_boot_id);
    TEST_ASSERT_TRUE(capability.enabled_features == ROBUSTO_PROXY_FEATURE_PUBSUB_V1);
    TEST_ASSERT_EQUAL_U32(1U, capability.pubsub_major);
    TEST_ASSERT_EQUAL_U32(1U, capability.pubsub_minor);
    TEST_ASSERT_EQUAL_U32(4096U, capability.selected_max_payload);
    TEST_ASSERT_EQUAL_U32(2U, capability.selected_max_in_flight);
    TEST_ASSERT_EQUAL_U32(16U, capability.max_subscriptions);

    service.session.state = ROBUSTO_PROXY_SESSION_RESET;
    response_size = 0U;
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY,
        NULL,
        0U,
        11U,
        response,
        sizeof(response),
        &response_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, (uint32_t)response_size);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_response_prefix(response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED, prefix.status);
    TEST_ASSERT_EQUAL_U32(1U, service.requests);
    TEST_ASSERT_EQUAL_U32(1U, service.errors);
}

static void test_service_control_frame_dispatch_happy_path_and_bad_crc(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_frame_header_t request_header;
    const robusto_proxy_frame_header_t *response_header;
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_health_response_t health;
    uint8_t request_frame[ROBUSTO_PROXY_HEADER_SIZE_BYTES + ROBUSTO_PROXY_CRC_SIZE_BYTES];
    uint8_t response_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    size_t request_size = 0U;
    size_t response_size = 0U;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0xABCDEFULL,
        1U,
        50U,
        2U,
        100U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    robusto_proxy_frame_header_init(
        &request_header,
        ROBUSTO_PROXY_FLAG_REQUEST,
        ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_HEALTH,
        0x10203040U,
        7U,
        0U);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_frame_encode(request_frame, sizeof(request_frame), &request_header, NULL, &request_size));

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_handle_frame(
            &service,
            request_frame,
            request_size,
            125U,
            response_frame,
            sizeof(response_frame),
            &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_frame_validate_buffer(response_frame, response_size, NULL));
    response_header = (const robusto_proxy_frame_header_t *)response_frame;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_FLAG_RESPONSE, response_header->flags);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_DOMAIN_CONTROL, response_header->domain);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_OPCODE_HEALTH, response_header->opcode);
    TEST_ASSERT_EQUAL_U32(0x10203040U, response_header->correlation_id);
    TEST_ASSERT_EQUAL_U32(50U, response_header->sequence);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_health_response_message(
            response_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
            response_header->payload_length,
            &prefix,
            &health));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_EQUAL_U32(25U, health.uptime_ms);

    request_frame[request_size - 1U] ^= 0xFFU;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_BAD_CRC,
        robusto_proxy_service_handle_frame(
            &service,
            request_frame,
            request_size,
            126U,
            response_frame,
            sizeof(response_frame),
            &response_size));
}

static void test_service_hello_negotiation_and_rejection_paths(void)
{
    const robusto_proxy_pubsub_adapter_t inline_only_adapter = {0};
    robusto_proxy_service_t service;
    robusto_proxy_hello_request_t hello_request;
    robusto_proxy_hello_response_t hello_response;
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_frame_header_t request_header;
    const robusto_proxy_frame_header_t *response_header;
    uint8_t hello_payload[ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES];
    uint8_t request_frame[ROBUSTO_PROXY_HEADER_SIZE_BYTES + ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES + ROBUSTO_PROXY_CRC_SIZE_BYTES];
    uint8_t response_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t error_response[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES];
    size_t request_size = 0U;
    size_t response_size = 0U;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0x8877665544332211ULL,
        1U,
        10U,
        2U,
        1000U);
    memset(&hello_request, 0, sizeof(hello_request));
    hello_request.controller_boot_id = 0x1122334455667788ULL;
    hello_request.min_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    hello_request.max_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    hello_request.min_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
    hello_request.max_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
    hello_request.max_payload = ROBUSTO_PROXY_MAX_PAYLOAD_BYTES;
    hello_request.max_in_flight = 8U;
    hello_request.required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(hello_payload, sizeof(hello_payload), &hello_request));
    robusto_proxy_frame_header_init(
        &request_header,
        ROBUSTO_PROXY_FLAG_REQUEST,
        ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_HELLO,
        77U,
        4U,
        sizeof(hello_payload));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_frame_encode(request_frame, sizeof(request_frame), &request_header, hello_payload, &request_size));

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_handle_frame(
            &service,
            request_frame,
            request_size,
            1025U,
            response_frame,
            sizeof(response_frame),
            &response_size));
    response_header = (const robusto_proxy_frame_header_t *)response_frame;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK, robusto_proxy_frame_validate_buffer(response_frame, response_size, NULL));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_OPCODE_HELLO, response_header->opcode);
    TEST_ASSERT_EQUAL_U32(77U, response_header->correlation_id);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_hello_response_message(
            response_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
            response_header->payload_length,
            &prefix,
            &hello_response));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_TRUE(hello_response.proxy_boot_id == service.session.local_boot_id);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PROTOCOL_MAJOR, hello_response.selected_protocol_major);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PROTOCOL_MINOR, hello_response.selected_protocol_minor);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_MAX_PAYLOAD_BYTES, hello_response.selected_max_payload);
    TEST_ASSERT_EQUAL_U32(2U, hello_response.selected_max_in_flight);
    TEST_ASSERT_EQUAL_U32(32U, hello_response.dedupe_entries);
    TEST_ASSERT_EQUAL_U32(25U, hello_response.proxy_uptime_ms);
    TEST_ASSERT_TRUE(hello_response.enabled_features == ROBUSTO_PROXY_FEATURE_PUBSUB_V1);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, service.session.state);
    TEST_ASSERT_TRUE(service.session.peer_boot_id == hello_request.controller_boot_id);
    TEST_ASSERT_EQUAL_U32(1U, service.requests);

    robusto_proxy_service_set_pubsub_adapter(
        &service, &inline_only_adapter, NULL);
    hello_request.optional_features =
        ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(
            hello_payload, sizeof(hello_payload), &hello_request));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload,
        sizeof(hello_payload),
        1026U,
        response_frame,
        sizeof(response_frame),
        &response_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_hello_response_message(
            response_frame, response_size, &prefix, &hello_response));
    TEST_ASSERT_TRUE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_TRUE(
        hello_response.enabled_features == ROBUSTO_PROXY_FEATURE_PUBSUB_V1);

    hello_request.required_features |=
        ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH;
    hello_request.optional_features = 0U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(
            hello_payload, sizeof(hello_payload), &hello_request));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload,
        sizeof(hello_payload),
        1027U,
        error_response,
        sizeof(error_response),
        &response_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_response_prefix(
            error_response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(
        ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE, prefix.status);
    hello_request.required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;

    hello_request.min_protocol_major = 2U;
    hello_request.max_protocol_major = 2U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(hello_payload, sizeof(hello_payload), &hello_request));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload,
        sizeof(hello_payload),
        1026U,
        error_response,
        sizeof(error_response),
        &response_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_response_prefix(error_response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_UNSUPPORTED_VERSION, prefix.status);

    hello_request.min_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    hello_request.max_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    hello_request.required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1 | 0x08ULL;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(hello_payload, sizeof(hello_payload), &hello_request));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload,
        sizeof(hello_payload),
        1027U,
        error_response,
        sizeof(error_response),
        &response_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_response_prefix(error_response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE, prefix.status);
    TEST_ASSERT_EQUAL_U32(5U, service.requests);
    TEST_ASSERT_EQUAL_U32(3U, service.errors);
}

static void test_service_control_handler_rejects_unsupported_opcode(void)
{
    robusto_proxy_service_t service;
    robusto_proxy_response_prefix_t prefix;
    uint8_t response[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES];
    size_t response_size = 0U;

    robusto_proxy_service_init(
        &service,
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        0x99ULL,
        1U,
        1U,
        2U,
        0U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service,
        0x7FU,
        NULL,
        0U,
        10U,
        response,
        sizeof(response),
        &response_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES, (uint32_t)response_size);
    TEST_ASSERT_EQUAL_U32(0U, service.requests);
    TEST_ASSERT_EQUAL_U32(1U, service.errors);

    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_decode_response_prefix(response, response_size, &prefix));
    TEST_ASSERT_FALSE(robusto_proxy_response_prefix_is_success(&prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_UNSUPPORTED_OPCODE, prefix.status);
}

static void test_pubsub_request_golden_vectors(void)
{
    static const uint8_t topic[] = "sensor.temp";
    static const uint8_t data[] = {0x19U, 0x2AU};
    static const uint8_t publish_golden[] = {
        0x2AU, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x0BU, 0U, 0U, 0U, 2U, 0U, 0U, 0U,
        's', 'e', 'n', 's', 'o', 'r', '.', 't', 'e', 'm', 'p', 0x19U, 0x2AU};
    static const uint8_t subscribe_golden[] = {
        0x2BU, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x0BU, 0U, 1U, 0U,
        's', 'e', 'n', 's', 'o', 'r', '.', 't', 'e', 'm', 'p'};
    static const uint8_t unsubscribe_golden[] = {
        0x2CU, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 0U, 0U, 0U};
    uint8_t buffer[64];
    size_t encoded_size = 0U;
    robusto_proxy_pubsub_publish_request_t publish = {
        .operation_id = 42U, .topic = topic, .topic_length = 11U,
        .data = data, .data_length = sizeof(data)};
    robusto_proxy_pubsub_publish_request_t decoded_publish;
    robusto_proxy_pubsub_subscribe_request_t subscribe = {
        .operation_id = 43U, .topic = topic, .topic_length = 11U,
        .options = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES};
    robusto_proxy_pubsub_subscribe_request_t decoded_subscribe;
    robusto_proxy_pubsub_unsubscribe_request_t unsubscribe = {
        .operation_id = 44U, .subscription_id = 7U};
    robusto_proxy_pubsub_unsubscribe_request_t decoded_unsubscribe;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_publish_request(buffer, sizeof(buffer), &publish, &encoded_size));
    TEST_ASSERT_EQUAL_U32(sizeof(publish_golden), (uint32_t)encoded_size);
    TEST_ASSERT_TRUE(memcmp(buffer, publish_golden, sizeof(publish_golden)) == 0);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_publish_request(buffer, encoded_size, &decoded_publish));
    TEST_ASSERT_EQUAL_U32(42U, (uint32_t)decoded_publish.operation_id);
    TEST_ASSERT_EQUAL_U32(11U, decoded_publish.topic_length);
    TEST_ASSERT_EQUAL_U32(2U, decoded_publish.data_length);
    TEST_ASSERT_TRUE(memcmp(decoded_publish.topic, topic, sizeof(topic) - 1U) == 0);
    TEST_ASSERT_TRUE(memcmp(decoded_publish.data, data, sizeof(data)) == 0);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_subscribe_request(buffer, sizeof(buffer), &subscribe, &encoded_size));
    TEST_ASSERT_EQUAL_U32(sizeof(subscribe_golden), (uint32_t)encoded_size);
    TEST_ASSERT_TRUE(memcmp(buffer, subscribe_golden, sizeof(subscribe_golden)) == 0);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_subscribe_request(buffer, encoded_size, &decoded_subscribe));
    TEST_ASSERT_EQUAL_U32(43U, (uint32_t)decoded_subscribe.operation_id);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES, decoded_subscribe.options);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_unsubscribe_request(buffer, sizeof(buffer), &unsubscribe));
    TEST_ASSERT_TRUE(memcmp(buffer, unsubscribe_golden, sizeof(unsubscribe_golden)) == 0);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_unsubscribe_request(buffer, sizeof(unsubscribe_golden), &decoded_unsubscribe));
    TEST_ASSERT_EQUAL_U32(7U, decoded_unsubscribe.subscription_id);
}

static void test_pubsub_response_and_delivery_round_trips(void)
{
    uint8_t buffer[64];
    size_t encoded_size = 0U;
    const uint8_t data[] = {0x19U, 0x2AU};
    robusto_proxy_pubsub_publish_response_t publish = {0x12345678U, UINT32_MAX};
    robusto_proxy_pubsub_publish_response_t decoded_publish;
    robusto_proxy_pubsub_subscribe_response_t subscribe = {7U, 0x12345678U, 1U};
    robusto_proxy_pubsub_subscribe_response_t decoded_subscribe;
    robusto_proxy_pubsub_unsubscribe_response_t unsubscribe = {1U};
    robusto_proxy_pubsub_unsubscribe_response_t decoded_unsubscribe;
    robusto_proxy_pubsub_status_response_t status = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U};
    robusto_proxy_pubsub_status_response_t decoded_status;
    robusto_proxy_pubsub_delivery_t delivery = {7U, 1U, data, sizeof(data)};
    robusto_proxy_pubsub_delivery_t decoded_delivery;
    robusto_proxy_pubsub_delivery_begin_t begin = {7U, 2U, 5000U};
    robusto_proxy_pubsub_delivery_begin_t decoded_begin;
    robusto_proxy_pubsub_delivery_chunk_t chunk = {7U, 2U, 4096U, data, sizeof(data)};
    robusto_proxy_pubsub_delivery_chunk_t decoded_chunk;
    robusto_proxy_pubsub_delivery_commit_t commit = {7U, 2U};
    robusto_proxy_pubsub_delivery_commit_t decoded_commit;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_publish_response(buffer, sizeof(buffer), &publish));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_publish_response(buffer, 8U, &decoded_publish));
    TEST_ASSERT_EQUAL_U32(UINT32_MAX, decoded_publish.delivery_count);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_subscribe_response(buffer, sizeof(buffer), &subscribe));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_subscribe_response(buffer, 12U, &decoded_subscribe));
    TEST_ASSERT_EQUAL_U32(7U, decoded_subscribe.subscription_id);
    TEST_ASSERT_EQUAL_U32(1U, decoded_subscribe.created);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_unsubscribe_response(buffer, sizeof(buffer), &unsubscribe));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_unsubscribe_response(buffer, 4U, &decoded_unsubscribe));
    TEST_ASSERT_EQUAL_U32(1U, decoded_unsubscribe.removed);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_status_response(buffer, sizeof(buffer), &status));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_status_response(buffer, 36U, &decoded_status));
    TEST_ASSERT_EQUAL_U32(2U, decoded_status.active_subscriptions);
    TEST_ASSERT_EQUAL_U32(9U, decoded_status.pubsub_errors);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_delivery(buffer, sizeof(buffer), &delivery, &encoded_size));
    TEST_ASSERT_EQUAL_U32(14U, (uint32_t)encoded_size);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery(buffer, encoded_size, &decoded_delivery));
    TEST_ASSERT_EQUAL_U32(7U, decoded_delivery.subscription_id);
    TEST_ASSERT_EQUAL_U32(1U, decoded_delivery.delivery_sequence);
    TEST_ASSERT_TRUE(memcmp(decoded_delivery.data, data, sizeof(data)) == 0);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_delivery_begin(
                              buffer, sizeof(buffer), &begin));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery_begin(
                              buffer, ROBUSTO_PROXY_PUBSUB_DELIVERY_BEGIN_SIZE_BYTES,
                              &decoded_begin));
    TEST_ASSERT_EQUAL_U32(5000U, decoded_begin.data_length);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_delivery_chunk(
                              buffer, sizeof(buffer), &chunk, &encoded_size));
    TEST_ASSERT_EQUAL_U32(18U, (uint32_t)encoded_size);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery_chunk(
                              buffer, encoded_size, &decoded_chunk));
    TEST_ASSERT_EQUAL_U32(4096U, decoded_chunk.offset);
    TEST_ASSERT_TRUE(memcmp(decoded_chunk.data, data, sizeof(data)) == 0);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_delivery_commit(
                              buffer, sizeof(buffer), &commit));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery_commit(
                              buffer, ROBUSTO_PROXY_PUBSUB_DELIVERY_COMMIT_SIZE_BYTES,
                              &decoded_commit));
    TEST_ASSERT_EQUAL_U32(2U, decoded_commit.delivery_sequence);
}

static void test_pubsub_codec_rejects_malformed_payloads(void)
{
    static const uint8_t topic[] = "valid.topic";
    uint8_t buffer[64];
    size_t encoded_size = 0U;
    robusto_proxy_pubsub_publish_request_t publish = {
        .operation_id = 1U, .topic = topic, .topic_length = 11U, .data = NULL, .data_length = 0U};
    robusto_proxy_pubsub_publish_request_t decoded_publish;
    robusto_proxy_pubsub_subscribe_request_t decoded_subscribe;

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_publish_request(buffer, sizeof(buffer), &publish, &encoded_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_LENGTH,
                          robusto_proxy_pubsub_decode_publish_request(buffer, encoded_size - 1U, &decoded_publish));
    buffer[10] = 1U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_RESERVED,
                          robusto_proxy_pubsub_decode_publish_request(buffer, encoded_size, &decoded_publish));
    buffer[10] = 0U;
    buffer[16] = 0x1FU;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT,
                          robusto_proxy_pubsub_decode_publish_request(buffer, encoded_size, &decoded_publish));

    memset(buffer, 0, sizeof(buffer));
    buffer[0] = 1U;
    buffer[8] = 1U;
    buffer[10] = 1U;
    buffer[11] = 1U;
    buffer[12] = 'a';
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_RESERVED,
                          robusto_proxy_pubsub_decode_subscribe_request(buffer, 13U, &decoded_subscribe));
    buffer[11] = 0U;
    buffer[10] = 0U;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT,
                          robusto_proxy_pubsub_decode_subscribe_request(buffer, 13U, &decoded_subscribe));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_BAD_LENGTH,
                          robusto_proxy_pubsub_decode_delivery(buffer, 11U, (robusto_proxy_pubsub_delivery_t *)&decoded_publish));
}

typedef struct fake_pubsub_adapter_state {
    uint32_t publish_calls;
    uint32_t publish_begin_calls;
    uint32_t publish_chunk_calls;
    uint32_t publish_commit_calls;
    uint32_t publish_abort_calls;
    uint32_t session_reset_calls;
    uint32_t publish_data_length;
    uint32_t publish_data_received;
    uint32_t subscribe_calls;
    uint32_t unsubscribe_calls;
    uint32_t status_calls;
    uint64_t last_operation_id;
    uint32_t subscription_id;
    uint16_t publish_begin_status;
    uint16_t publish_commit_status;
} fake_pubsub_adapter_state_t;

static uint16_t fake_pubsub_publish(void *context,
                                    const robusto_proxy_pubsub_publish_request_t *request,
                                    robusto_proxy_pubsub_publish_response_t *response)
{
    fake_pubsub_adapter_state_t *state = context;
    state->publish_calls += 1U;
    state->last_operation_id = request->operation_id;
    response->topic_hash = 0xAABBCCDDU;
    response->delivery_count = request->data_length;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_publish_begin(
    void *context, const robusto_proxy_pubsub_publish_begin_request_t *request)
{
    fake_pubsub_adapter_state_t *state = context;
    state->publish_begin_calls += 1U;
    if (state->publish_begin_status != ROBUSTO_PROXY_STATUS_OK)
    {
        return state->publish_begin_status;
    }
    state->last_operation_id = request->operation_id;
    state->publish_data_length = request->data_length;
    state->publish_data_received = 0U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_publish_chunk(
    void *context, const robusto_proxy_pubsub_publish_chunk_request_t *request)
{
    fake_pubsub_adapter_state_t *state = context;
    if (request->operation_id != state->last_operation_id ||
        request->offset != state->publish_data_received ||
        request->data_length > state->publish_data_length - state->publish_data_received)
    {
        return ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
    }
    state->publish_chunk_calls += 1U;
    state->publish_data_received += request->data_length;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_publish_commit(
    void *context, const robusto_proxy_pubsub_publish_transfer_request_t *request,
    robusto_proxy_pubsub_publish_response_t *response)
{
    fake_pubsub_adapter_state_t *state = context;
    if (request->operation_id != state->last_operation_id ||
        state->publish_data_received != state->publish_data_length)
    {
        return ROBUSTO_PROXY_STATUS_CONFLICT;
    }
    state->publish_commit_calls += 1U;
    state->publish_calls += 1U;
    if (state->publish_commit_status != ROBUSTO_PROXY_STATUS_OK)
    {
        return state->publish_commit_status;
    }
    response->topic_hash = 0xAABBCCDDU;
    response->delivery_count = state->publish_data_length;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_publish_abort(
    void *context, const robusto_proxy_pubsub_publish_transfer_request_t *request)
{
    fake_pubsub_adapter_state_t *state = context;
    if (request->operation_id != state->last_operation_id)
    {
        return ROBUSTO_PROXY_STATUS_CONFLICT;
    }
    state->publish_abort_calls += 1U;
    state->publish_data_length = 0U;
    state->publish_data_received = 0U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_session_reset(void *context)
{
    fake_pubsub_adapter_state_t *state = context;
    state->session_reset_calls += 1U;
    state->publish_data_length = 0U;
    state->publish_data_received = 0U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_subscribe(void *context,
                                      const robusto_proxy_pubsub_subscribe_request_t *request,
                                      robusto_proxy_pubsub_subscribe_response_t *response)
{
    fake_pubsub_adapter_state_t *state = context;
    state->subscribe_calls += 1U;
    response->subscription_id = state->subscription_id == 0U
                                    ? 7U
                                    : state->subscription_id;
    response->topic_hash = request->topic_length;
    response->created = 1U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_unsubscribe(void *context,
                                        const robusto_proxy_pubsub_unsubscribe_request_t *request,
                                        robusto_proxy_pubsub_unsubscribe_response_t *response)
{
    fake_pubsub_adapter_state_t *state = context;
    state->unsubscribe_calls += 1U;
    response->removed = request->subscription_id == 7U ? 1U : 0U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_pubsub_status(void *context,
                                   robusto_proxy_pubsub_status_response_t *response)
{
    fake_pubsub_adapter_state_t *state = context;
    state->status_calls += 1U;
    response->state = 1U;
    response->publish_requests = state->publish_calls;
    return ROBUSTO_PROXY_STATUS_OK;
}

typedef enum fake_client_exchange_mode {
    FAKE_CLIENT_EXCHANGE_RESPONSE = 0,
    FAKE_CLIENT_EXCHANGE_ACCEPTED_TIMEOUT,
    FAKE_CLIENT_EXCHANGE_NOT_ACCEPTED,
    FAKE_CLIENT_EXCHANGE_TIMEOUT_ONCE,
    FAKE_CLIENT_EXCHANGE_CHUNK_TIMEOUT_ONCE,
} fake_client_exchange_mode_t;

typedef struct fake_client_transport {
    robusto_proxy_service_t *service;
    uint32_t now_ms;
    uint32_t exchanges;
    uint32_t last_timeout_ms;
    uint32_t wait_calls;
    uint32_t waited_ms;
    uint32_t jitter_ms;
    uint32_t timeouts_remaining;
    uint32_t first_correlation_id;
    uint32_t last_correlation_id;
    uint32_t first_sequence;
    uint32_t last_sequence;
    uint8_t last_flags;
    fake_client_exchange_mode_t mode;
} fake_client_transport_t;

static uint32_t fake_client_now_ms(void *context)
{
    return ((fake_client_transport_t *)context)->now_ms;
}

static void fake_client_wait_ms(void *context, uint32_t delay_ms)
{
    fake_client_transport_t *transport = context;
    transport->wait_calls += 1U;
    transport->waited_ms += delay_ms;
    transport->now_ms += delay_ms;
}

static uint32_t fake_client_retry_jitter_ms(void *context, uint32_t maximum_ms)
{
    fake_client_transport_t *transport = context;
    return transport->jitter_ms < maximum_ms ? transport->jitter_ms : maximum_ms;
}

static rob_ret_val_t fake_client_exchange(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size,
    uint32_t timeout_ms,
    robusto_proxy_transfer_acceptance_t *acceptance)
{
    fake_client_transport_t *transport = context;
    const robusto_proxy_frame_header_t *header =
        (const robusto_proxy_frame_header_t *)request;

    transport->exchanges += 1U;
    transport->last_timeout_ms = timeout_ms;
    if (transport->first_correlation_id == 0U)
    {
        transport->first_correlation_id = header->correlation_id;
        transport->first_sequence = header->sequence;
    }
    transport->last_correlation_id = header->correlation_id;
    transport->last_sequence = header->sequence;
    transport->last_flags = header->flags;
    if (transport->mode == FAKE_CLIENT_EXCHANGE_NOT_ACCEPTED)
    {
        *acceptance = ROBUSTO_PROXY_TRANSFER_NOT_ACCEPTED;
        *response_size = 0U;
        return ROB_ERR_SEND_FAIL;
    }
    if (robusto_proxy_service_handle_frame(
            transport->service, request, request_size, transport->now_ms,
            response, response_capacity, response_size) != ROBUSTO_PROXY_RESULT_OK)
    {
        *acceptance = ROBUSTO_PROXY_TRANSFER_ACCEPTANCE_UNKNOWN;
        return ROB_ERR_PARSING_FAILED;
    }
    *acceptance = ROBUSTO_PROXY_TRANSFER_ACCEPTED;
    if (transport->mode == FAKE_CLIENT_EXCHANGE_ACCEPTED_TIMEOUT)
    {
        *response_size = 0U;
        return ROB_ERR_TIMEOUT;
    }
    if (transport->mode == FAKE_CLIENT_EXCHANGE_CHUNK_TIMEOUT_ONCE &&
        header->domain == ROBUSTO_PROXY_DOMAIN_PUBSUB &&
        header->opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_CHUNK &&
        transport->timeouts_remaining > 0U)
    {
        transport->timeouts_remaining -= 1U;
        *response_size = 0U;
        return ROB_ERR_TIMEOUT;
    }
    if (transport->mode == FAKE_CLIENT_EXCHANGE_TIMEOUT_ONCE &&
        transport->timeouts_remaining > 0U)
    {
        transport->timeouts_remaining -= 1U;
        *response_size = 0U;
        return ROB_ERR_TIMEOUT;
    }
    return ROB_OK;
}

static rob_ret_val_t fake_proxy_delivery_callback(
    void *context,
    uint8_t *data,
    uint32_t data_length)
{
    uint32_t *callback_count = context;
    (void)data;
    (void)data_length;
    *callback_count += 1U;
    return ROB_OK;
}

static void test_proxy_client_connect_publish_and_acceptance(void)
{
    static const robusto_proxy_pubsub_adapter_t adapter = {
        .publish = fake_pubsub_publish,
        .publish_begin = fake_pubsub_publish_begin,
        .publish_chunk = fake_pubsub_publish_chunk,
        .publish_commit = fake_pubsub_publish_commit,
        .publish_abort = fake_pubsub_publish_abort,
        .session_reset = fake_pubsub_session_reset,
        .subscribe = fake_pubsub_subscribe,
        .unsubscribe = fake_pubsub_unsubscribe,
        .status = fake_pubsub_status,
    };
    uint8_t request_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t response_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t delivery_payload[ROBUSTO_PROXY_MAX_PAYLOAD_BYTES];
    uint8_t delivery_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t data[] = {0x11U, 0x22U};
    static uint8_t large_data[200U * 1024U];
    robusto_proxy_pubsub_client_subscription_t subscription_storage[1];
    robusto_proxy_pubsub_client_subscription_t *subscription = NULL;
    robusto_proxy_service_t service;
    fake_pubsub_adapter_state_t adapter_state = {0};
    fake_client_transport_t transport = {0};
    robusto_proxy_client_t client;
    robusto_proxy_health_response_t health;
    robusto_proxy_capability_response_t capabilities;
    robusto_proxy_pubsub_status_response_t pubsub_status;
    robusto_proxy_pubsub_delivery_t delivery = {
        .subscription_id = 7U,
        .delivery_sequence = 1U,
        .data = data,
        .data_length = sizeof(data),
    };
    uint32_t callback_count = 0U;
    size_t delivery_payload_size = 0U;
    size_t delivery_frame_size = 0U;
    robusto_proxy_client_config_t config = {
        .profile = ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
        .controller_boot_id = 0x1122334455667788ULL,
        .correlation_seed = 10U,
        .sequence_seed = 20U,
        .operation_seed = 30U,
        .request_timeout_ms = 1000U,
        .exchange = fake_client_exchange,
        .transport_context = &transport,
        .now_ms = fake_client_now_ms,
        .wait_ms = fake_client_wait_ms,
        .retry_jitter_ms = fake_client_retry_jitter_ms,
        .clock_context = &transport,
        .request_frame = request_frame,
        .request_frame_size = sizeof(request_frame),
        .response_frame = response_frame,
        .response_frame_size = sizeof(response_frame),
    };

    robusto_proxy_service_init(&service, ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
                               0xC6U, 1U, 1U, 2U, 0U);
    robusto_proxy_service_set_pubsub_adapter(&service, &adapter, &adapter_state);
    transport.service = &service;
    transport.now_ms = 100U;

    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_init(&client, &config));
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_configure(&client, subscription_storage, 1U));
    TEST_ASSERT_FALSE(robusto_proxy_pubsub_is_ready(&client));
    TEST_ASSERT_EQUAL_INT(ROB_ERR_NOT_READY,
                          robusto_proxy_pubsub_publish(&client, "client.test", data, sizeof(data)));
    TEST_ASSERT_EQUAL_U32(0U, transport.exchanges);

    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_connect(&client));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_is_ready(&client));
    TEST_ASSERT_EQUAL_U32(1U, transport.exchanges);
    TEST_ASSERT_EQUAL_U32(1000U, transport.last_timeout_ms);
    TEST_ASSERT_EQUAL_INT(ROB_OK,
                          robusto_proxy_pubsub_publish(&client, "client.test", data, sizeof(data)));
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.publish_calls);
    TEST_ASSERT_EQUAL_U32(30U, (uint32_t)adapter_state.last_operation_id);
    for (size_t index = 0U; index < sizeof(large_data); ++index)
    {
        large_data[index] = (uint8_t)index;
    }
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_publish(&client, "client.large", large_data,
                                     sizeof(large_data)));
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.publish_calls);
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.publish_begin_calls);
    TEST_ASSERT_EQUAL_U32(51U, adapter_state.publish_chunk_calls);
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.publish_commit_calls);
    TEST_ASSERT_EQUAL_U32(sizeof(large_data), adapter_state.publish_data_received);

    adapter_state.publish_commit_status = ROBUSTO_PROXY_STATUS_INTERNAL;
    TEST_ASSERT_EQUAL_INT(
        ROB_FAIL,
        robusto_proxy_pubsub_publish(&client, "client.large", large_data,
                                     sizeof(large_data)));
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.publish_commit_calls);
    TEST_ASSERT_EQUAL_U32(0U, adapter_state.publish_abort_calls);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, client.session.state);
    adapter_state.publish_commit_status = ROBUSTO_PROXY_STATUS_OK;

    transport.mode = FAKE_CLIENT_EXCHANGE_CHUNK_TIMEOUT_ONCE;
    transport.timeouts_remaining = 1U;
    TEST_ASSERT_EQUAL_INT(
        ROB_FAIL,
        robusto_proxy_pubsub_publish(&client, "client.large", large_data,
                                     sizeof(large_data)));
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.publish_abort_calls);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, client.session.state);
    transport.mode = FAKE_CLIENT_EXCHANGE_RESPONSE;

    adapter_state.publish_begin_status = ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY;
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_OUT_OF_MEMORY,
        robusto_proxy_pubsub_publish(&client, "client.large", large_data,
                                     sizeof(large_data)));
    TEST_ASSERT_EQUAL_U32(4U, adapter_state.publish_begin_calls);
    TEST_ASSERT_EQUAL_U32(103U, adapter_state.publish_chunk_calls);
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.publish_commit_calls);
    adapter_state.publish_begin_status = ROBUSTO_PROXY_STATUS_OK;
    transport.mode = FAKE_CLIENT_EXCHANGE_ACCEPTED_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_OUTCOME_UNKNOWN,
        robusto_proxy_pubsub_publish(&client, "client.large", large_data,
                                     sizeof(large_data)));
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.publish_abort_calls);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_DEGRADED, client.session.state);
    transport.mode = FAKE_CLIENT_EXCHANGE_RESPONSE;
    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_connect(&client));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED, client.session.state);
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.session_reset_calls);
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_query_status(&client, &pubsub_status));
    TEST_ASSERT_EQUAL_U32(1U, pubsub_status.state);
    TEST_ASSERT_EQUAL_U32(3U, pubsub_status.publish_requests);
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.status_calls);

    transport.mode = FAKE_CLIENT_EXCHANGE_ACCEPTED_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(ROB_ERR_OUTCOME_UNKNOWN,
                          robusto_proxy_pubsub_publish(&client, "client.test", data, sizeof(data)));
    TEST_ASSERT_EQUAL_U32(4U, adapter_state.publish_calls);

    transport.mode = FAKE_CLIENT_EXCHANGE_NOT_ACCEPTED;
    TEST_ASSERT_EQUAL_INT(ROB_ERR_SEND_FAIL,
                          robusto_proxy_pubsub_publish(&client, "client.test", data, sizeof(data)));
    TEST_ASSERT_EQUAL_U32(4U, adapter_state.publish_calls);
    TEST_ASSERT_EQUAL_U32(0U, client.inflight.active_count);

    transport.mode = FAKE_CLIENT_EXCHANGE_RESPONSE;
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_subscribe(&client, "client.delivery",
                                       fake_proxy_delivery_callback, &callback_count,
                                       &subscription));
    TEST_ASSERT_TRUE(subscription != NULL);
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.subscribe_calls);
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_QUEUE_FULL,
        robusto_proxy_pubsub_subscribe(&client, "client.second",
                                       fake_proxy_delivery_callback, &callback_count,
                                       &subscription));
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.subscribe_calls);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_pubsub_encode_delivery(delivery_payload, sizeof(delivery_payload),
                                             &delivery, &delivery_payload_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_build_pubsub_delivery_event(
            &service, delivery_payload, delivery_payload_size,
            delivery_frame, sizeof(delivery_frame), &delivery_frame_size));
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));
    TEST_ASSERT_EQUAL_U32(1U, callback_count);
    TEST_ASSERT_EQUAL_U32(1U, client.pubsub_delivery_events);

    delivery.delivery_sequence = 3U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_pubsub_encode_delivery(delivery_payload, sizeof(delivery_payload),
                                             &delivery, &delivery_payload_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_build_pubsub_delivery_event(
            &service, delivery_payload, delivery_payload_size,
            delivery_frame, sizeof(delivery_frame), &delivery_frame_size));
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));
    TEST_ASSERT_EQUAL_U32(2U, callback_count);
    TEST_ASSERT_EQUAL_U32(1U, client.pubsub_delivery_sequence_gaps);

    delivery.subscription_id = 8U;
    delivery.delivery_sequence = 1U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_pubsub_encode_delivery(delivery_payload, sizeof(delivery_payload),
                                             &delivery, &delivery_payload_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_build_pubsub_delivery_event(
            &service, delivery_payload, delivery_payload_size,
            delivery_frame, sizeof(delivery_frame), &delivery_frame_size));
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_INVALID_ID,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));
    TEST_ASSERT_EQUAL_U32(2U, callback_count);
    TEST_ASSERT_EQUAL_U32(1U, client.pubsub_unknown_deliveries);
    TEST_ASSERT_EQUAL_INT(ROB_OK,
                          robusto_proxy_pubsub_unsubscribe(&client, subscription_storage));
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.unsubscribe_calls);
    TEST_ASSERT_EQUAL_INT(ROB_ERR_INVALID_ID,
                          robusto_proxy_pubsub_unsubscribe(&client, subscription_storage));
    TEST_ASSERT_EQUAL_U32(1U, adapter_state.unsubscribe_calls);

    transport.mode = FAKE_CLIENT_EXCHANGE_TIMEOUT_ONCE;
    transport.timeouts_remaining = 1U;
    transport.jitter_ms = 7U;
    transport.first_correlation_id = 0U;
    transport.wait_calls = 0U;
    transport.waited_ms = 0U;
    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_query_health(&client, &health));
    TEST_ASSERT_EQUAL_U32(0xC6U, (uint32_t)health.proxy_boot_id);
    TEST_ASSERT_EQUAL_U32(transport.first_correlation_id,
                          transport.last_correlation_id);
    TEST_ASSERT_TRUE(transport.first_sequence != transport.last_sequence);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_FLAG_REQUEST | ROBUSTO_PROXY_FLAG_RETRY,
                          transport.last_flags);
    TEST_ASSERT_EQUAL_U32(1U, transport.wait_calls);
    TEST_ASSERT_EQUAL_U32(57U, transport.waited_ms);
    TEST_ASSERT_EQUAL_U32(1U, client.retries);

    transport.mode = FAKE_CLIENT_EXCHANGE_RESPONSE;
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_client_query_capabilities(&client, &capabilities));
    TEST_ASSERT_EQUAL_U32(0xC6U, (uint32_t)capabilities.proxy_boot_id);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_FEATURE_PUBSUB_V1 |
                              ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH |
                              ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_DELIVERY,
                          (uint32_t)capabilities.enabled_features);

    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_subscribe(&client, "client.reconcile",
                                       fake_proxy_delivery_callback, &callback_count,
                                       &subscription));
    TEST_ASSERT_EQUAL_U32(2U, adapter_state.subscribe_calls);
    adapter_state.subscription_id = 9U;
    service.session.local_boot_id = 0xC7U;
    TEST_ASSERT_EQUAL_INT(ROB_ERR_NOT_READY,
                          robusto_proxy_client_query_health(&client, &health));
    TEST_ASSERT_FALSE(robusto_proxy_pubsub_is_ready(&client));
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_NOT_READY,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));

    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_connect(&client));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_is_ready(&client));
    TEST_ASSERT_EQUAL_U32(3U, adapter_state.subscribe_calls);

    delivery.subscription_id = 7U;
    delivery.delivery_sequence = 1U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_pubsub_encode_delivery(delivery_payload, sizeof(delivery_payload),
                                             &delivery, &delivery_payload_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_build_pubsub_delivery_event(
            &service, delivery_payload, delivery_payload_size,
            delivery_frame, sizeof(delivery_frame), &delivery_frame_size));
    TEST_ASSERT_EQUAL_INT(
        ROB_ERR_INVALID_ID,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));

    delivery.subscription_id = 9U;
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_pubsub_encode_delivery(delivery_payload, sizeof(delivery_payload),
                                             &delivery, &delivery_payload_size));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_service_build_pubsub_delivery_event(
            &service, delivery_payload, delivery_payload_size,
            delivery_frame, sizeof(delivery_frame), &delivery_frame_size));
    TEST_ASSERT_EQUAL_INT(
        ROB_OK,
        robusto_proxy_pubsub_handle_event(&client, delivery_frame, delivery_frame_size));
    TEST_ASSERT_EQUAL_U32(3U, callback_count);
    TEST_ASSERT_EQUAL_U32(2U, client.pubsub_unknown_deliveries);

    {
        robusto_proxy_pubsub_delivery_begin_t begin = {9U, 2U, 5000U};
        robusto_proxy_pubsub_delivery_chunk_t chunk = {
            9U, 2U, 0U, large_data, 4080U};
        robusto_proxy_pubsub_delivery_commit_t commit = {9U, 2U};

        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_encode_delivery_begin(
                                  delivery_payload, sizeof(delivery_payload), &begin));
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_service_build_pubsub_event(
                                  &service, ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_BEGIN,
                                  delivery_payload,
                                  ROBUSTO_PROXY_PUBSUB_DELIVERY_BEGIN_SIZE_BYTES,
                                  delivery_frame, sizeof(delivery_frame),
                                  &delivery_frame_size));
        TEST_ASSERT_EQUAL_INT(ROB_OK,
                              robusto_proxy_pubsub_handle_event(
                                  &client, delivery_frame, delivery_frame_size));
        TEST_ASSERT_EQUAL_U32(3U, callback_count);

        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_encode_delivery_chunk(
                                  delivery_payload, sizeof(delivery_payload),
                                  &chunk, &delivery_payload_size));
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_service_build_pubsub_event(
                                  &service, ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK,
                                  delivery_payload, delivery_payload_size,
                                  delivery_frame, sizeof(delivery_frame),
                                  &delivery_frame_size));
        TEST_ASSERT_EQUAL_INT(ROB_OK,
                              robusto_proxy_pubsub_handle_event(
                                  &client, delivery_frame, delivery_frame_size));

        chunk.offset = 4080U;
        chunk.data = large_data + 4080U;
        chunk.data_length = 920U;
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_encode_delivery_chunk(
                                  delivery_payload, sizeof(delivery_payload),
                                  &chunk, &delivery_payload_size));
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_service_build_pubsub_event(
                                  &service, ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK,
                                  delivery_payload, delivery_payload_size,
                                  delivery_frame, sizeof(delivery_frame),
                                  &delivery_frame_size));
        TEST_ASSERT_EQUAL_INT(ROB_OK,
                              robusto_proxy_pubsub_handle_event(
                                  &client, delivery_frame, delivery_frame_size));

        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_encode_delivery_commit(
                                  delivery_payload, sizeof(delivery_payload), &commit));
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_service_build_pubsub_event(
                                  &service, ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_COMMIT,
                                  delivery_payload,
                                  ROBUSTO_PROXY_PUBSUB_DELIVERY_COMMIT_SIZE_BYTES,
                                  delivery_frame, sizeof(delivery_frame),
                                  &delivery_frame_size));
        TEST_ASSERT_EQUAL_INT(ROB_OK,
                              robusto_proxy_pubsub_handle_event(
                                  &client, delivery_frame, delivery_frame_size));
        TEST_ASSERT_EQUAL_U32(4U, callback_count);
        TEST_ASSERT_EQUAL_U32(4U, client.pubsub_delivery_events);
        TEST_ASSERT_TRUE(client.pubsub_delivery_data == NULL);
    }

    transport.mode = FAKE_CLIENT_EXCHANGE_ACCEPTED_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(ROB_ERR_TIMEOUT,
                          robusto_proxy_client_query_health(&client, &health));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED,
                          client.session.state);
    TEST_ASSERT_EQUAL_INT(ROB_ERR_TIMEOUT,
                          robusto_proxy_client_query_health(&client, &health));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED,
                          client.session.state);
    TEST_ASSERT_EQUAL_INT(ROB_ERR_TIMEOUT,
                          robusto_proxy_client_query_health(&client, &health));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_DEGRADED,
                          client.session.state);
    TEST_ASSERT_FALSE(robusto_proxy_pubsub_is_ready(&client));

    transport.mode = FAKE_CLIENT_EXCHANGE_RESPONSE;
    TEST_ASSERT_EQUAL_INT(ROB_OK, robusto_proxy_client_connect(&client));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_SESSION_ESTABLISHED,
                          client.session.state);
    TEST_ASSERT_EQUAL_U32(0U, client.consecutive_health_timeouts);
}

static void test_service_pubsub_dispatch_and_gates(void)
{
    static const uint8_t topic[] = "sensor.temp";
    static const uint8_t data[] = {0x19U, 0x2AU};
    robusto_proxy_service_t service;
    fake_pubsub_adapter_state_t state = {0};
    const robusto_proxy_pubsub_adapter_t adapter = {
        .publish = fake_pubsub_publish,
        .subscribe = fake_pubsub_subscribe,
        .unsubscribe = fake_pubsub_unsubscribe,
        .status = fake_pubsub_status,
    };
    robusto_proxy_pubsub_publish_request_t publish = {
        .operation_id = 42U, .topic = topic, .topic_length = 11U,
        .data = data, .data_length = sizeof(data)};
    robusto_proxy_pubsub_publish_response_t decoded_publish;
    robusto_proxy_pubsub_subscribe_request_t subscribe = {
        .operation_id = 43U, .topic = topic, .topic_length = 11U,
        .options = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES};
    robusto_proxy_pubsub_subscribe_response_t decoded_subscribe;
    robusto_proxy_pubsub_unsubscribe_request_t unsubscribe = {
        .operation_id = 44U, .subscription_id = 7U};
    robusto_proxy_pubsub_unsubscribe_response_t decoded_unsubscribe;
    robusto_proxy_pubsub_status_response_t decoded_status;
    robusto_proxy_response_prefix_t prefix;
    uint8_t request[64];
    uint8_t response[64];
    size_t request_size = 0U;
    size_t response_size = 0U;

    robusto_proxy_service_init(&service, ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
                               1U, 1U, 1U, 2U, 0U);
    robusto_proxy_service_set_pubsub_adapter(&service, &adapter, &state);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_publish_request(request, sizeof(request), &publish, &request_size));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH, request, request_size,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_decode_response_prefix(response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED, prefix.status);
    TEST_ASSERT_EQUAL_U32(0U, state.publish_calls);

    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.session.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH, request, request_size,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_decode_response_prefix(response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK, prefix.status);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_publish_response(
                              response + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              response_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              &decoded_publish));
    TEST_ASSERT_EQUAL_U32(0xAABBCCDDU, decoded_publish.topic_hash);
    TEST_ASSERT_EQUAL_U32(2U, decoded_publish.delivery_count);
    TEST_ASSERT_EQUAL_U32(1U, state.publish_calls);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_subscribe_request(request, sizeof(request), &subscribe, &request_size));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_SUBSCRIBE, request, request_size,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_subscribe_response(
                              response + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              response_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              &decoded_subscribe));
    TEST_ASSERT_EQUAL_U32(7U, decoded_subscribe.subscription_id);
    TEST_ASSERT_EQUAL_U32(11U, decoded_subscribe.topic_hash);
    TEST_ASSERT_EQUAL_U32(1U, state.subscribe_calls);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_unsubscribe_request(request, sizeof(request), &unsubscribe));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_UNSUBSCRIBE, request,
        ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_REQUEST_SIZE_BYTES,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_unsubscribe_response(
                              response + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              response_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              &decoded_unsubscribe));
    TEST_ASSERT_EQUAL_U32(1U, decoded_unsubscribe.removed);
    TEST_ASSERT_EQUAL_U32(1U, state.unsubscribe_calls);

    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_STATUS, NULL, 0U,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_status_response(
                              response + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              response_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                              &decoded_status));
    TEST_ASSERT_EQUAL_U32(1U, decoded_status.state);
    TEST_ASSERT_EQUAL_U32(1U, decoded_status.publish_requests);
    TEST_ASSERT_EQUAL_U32(1U, state.status_calls);

    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_encode_publish_request(request, sizeof(request), &publish, &request_size));
    request[16] = 0x1FU;
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_pubsub_request(
        &service, ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH, request, request_size,
        response, sizeof(response), &response_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_decode_response_prefix(response, response_size, &prefix));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID, prefix.status);
    TEST_ASSERT_EQUAL_U32(1U, state.publish_calls);
}

typedef struct fake_server_backend_state {
    robusto_proxy_pubsub_local_callback_t callback;
    void *callback_context;
    uint32_t subscribe_calls;
    uint32_t unsubscribe_calls;
    uint32_t publish_calls;
    uint32_t published_length;
    uint32_t published_sum;
    uint16_t publish_status;
    uint16_t unsubscribe_status;
} fake_server_backend_state_t;

static uint16_t fake_server_publish(void *context, const char *topic,
                                    const uint8_t *data, uint32_t data_length,
                                    uint32_t *topic_hash, uint32_t *delivery_count)
{
    fake_server_backend_state_t *state = context;
    (void)topic;
    if (state->publish_status != ROBUSTO_PROXY_STATUS_OK)
    {
        return state->publish_status;
    }
    state->publish_calls += 1U;
    state->published_length = data_length;
    state->published_sum = 0U;
    for (uint32_t index = 0U; index < data_length; ++index)
    {
        state->published_sum += data[index];
    }
    *topic_hash = 0x12345678U;
    *delivery_count = state->callback == NULL ? 0U : 1U;
    return state->callback == NULL
               ? ROBUSTO_PROXY_STATUS_OK
               : state->callback(state->callback_context, data, data_length);
}

static uint16_t fake_server_subscribe(
    void *context, const char *topic,
    robusto_proxy_pubsub_local_callback_t callback,
    void *callback_context, uint32_t *topic_hash);
static uint16_t fake_server_unsubscribe(
    void *context, uint32_t topic_hash,
    robusto_proxy_pubsub_local_callback_t callback,
    void *callback_context);

static void test_pubsub_server_adapter_chunked_publish(void)
{
    static const uint8_t topic[] = "camera.jpeg";
    static uint8_t data[5000];
    const robusto_proxy_pubsub_backend_t backend = {
        fake_server_publish, fake_server_subscribe, fake_server_unsubscribe};
    fake_server_backend_state_t backend_state = {0};
    robusto_proxy_pubsub_server_adapter_t adapter;
    robusto_proxy_pubsub_subscription_t subscriptions[1];
    uint8_t event_pool[4];
    robusto_proxy_pubsub_publish_begin_request_t begin = {
        77U, topic, sizeof(topic) - 1U, sizeof(data)};
    robusto_proxy_pubsub_publish_chunk_request_t chunk = {
        77U, 0U, data, ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES};
    robusto_proxy_pubsub_publish_transfer_request_t transfer = {77U};
    robusto_proxy_pubsub_publish_response_t response;
    robusto_proxy_pubsub_subscribe_request_t subscribe = {
        78U, topic, sizeof(topic) - 1U, ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES};
    robusto_proxy_pubsub_subscribe_response_t subscribe_response;
    robusto_proxy_service_t service;
    robusto_proxy_hello_request_t hello = {
        .controller_boot_id = 1U,
        .min_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR,
        .max_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR,
        .min_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR,
        .max_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR,
        .max_payload = ROBUSTO_PROXY_MAX_PAYLOAD_BYTES,
        .max_in_flight = 1U,
        .required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1 |
                             ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH,
    };
    uint8_t hello_payload[ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES];
    uint8_t hello_response[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES +
                           ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES];
    size_t hello_response_size = 0U;
    const robusto_proxy_pubsub_adapter_t *operations =
        robusto_proxy_pubsub_server_adapter_operations();
    uint32_t expected_sum = 0U;
    uint8_t *publish_buffer;

    for (size_t index = 0U; index < sizeof(data); ++index)
    {
        data[index] = (uint8_t)index;
        expected_sum += data[index];
    }
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_init(
        &adapter, &backend, &backend_state, subscriptions, 1U,
        event_pool, sizeof(event_pool), (robusto_proxy_pubsub_lock_t){0}));
    robusto_proxy_service_init(&service, ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
                               2U, 1U, 1U, 1U, 0U);
    robusto_proxy_service_set_pubsub_adapter(&service, operations, &adapter);
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(
            hello_payload, sizeof(hello_payload), &hello));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service, ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload, sizeof(hello_payload), 1U,
        hello_response, sizeof(hello_response), &hello_response_size));
    TEST_ASSERT_EQUAL_U32(sizeof(hello_response), hello_response_size);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->subscribe(
                              &adapter, &subscribe, &subscribe_response));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_begin(&adapter, &begin));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_BUSY,
                          operations->publish_begin(&adapter, &begin));
    chunk.offset = 1U;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD,
                          operations->publish_chunk(&adapter, &chunk));
    chunk.offset = 0U;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_chunk(&adapter, &chunk));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD,
                          operations->publish_commit(&adapter, &transfer, &response));
    chunk.offset = ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES;
    chunk.data += ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES;
    chunk.data_length = sizeof(data) - ROBUSTO_PROXY_PUBSUB_MAX_CHUNK_DATA_BYTES;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_chunk(&adapter, &chunk));
    publish_buffer = adapter.publish_data;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_commit(&adapter, &transfer, &response));
    TEST_ASSERT_EQUAL_U32(1U, backend_state.publish_calls);
    TEST_ASSERT_EQUAL_U32(sizeof(data), backend_state.published_length);
    TEST_ASSERT_EQUAL_U32(expected_sum, backend_state.published_sum);
    TEST_ASSERT_TRUE(adapter.publish_data == NULL);
    TEST_ASSERT_EQUAL_U32(1U, adapter.event_count);
    TEST_ASSERT_TRUE(adapter.events[adapter.event_read_index].transfer_data ==
                     publish_buffer);

    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_begin(&adapter, &begin));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_abort(&adapter, &transfer));
    TEST_ASSERT_TRUE(adapter.publish_data == NULL);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_begin(&adapter, &begin));
    TEST_ASSERT_EQUAL_INT(
        ROBUSTO_PROXY_RESULT_OK,
        robusto_proxy_encode_hello_request(
            hello_payload, sizeof(hello_payload), &hello));
    TEST_ASSERT_TRUE(robusto_proxy_service_handle_control_request(
        &service, ROBUSTO_PROXY_OPCODE_HELLO,
        hello_payload, sizeof(hello_payload), 2U,
        hello_response, sizeof(hello_response), &hello_response_size));
    TEST_ASSERT_EQUAL_U32(sizeof(hello_response), hello_response_size);
    TEST_ASSERT_TRUE(adapter.publish_data == NULL);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_begin(&adapter, &begin));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish_abort(&adapter, &transfer));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_deinit(&adapter));
}

static uint16_t fake_server_subscribe(void *context, const char *topic,
                                      robusto_proxy_pubsub_local_callback_t callback,
                                      void *callback_context, uint32_t *topic_hash)
{
    fake_server_backend_state_t *state = context;
    (void)topic;
    state->callback = callback;
    state->callback_context = callback_context;
    state->subscribe_calls += 1U;
    *topic_hash = 0x12345678U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static uint16_t fake_server_unsubscribe(void *context, uint32_t topic_hash,
                                        robusto_proxy_pubsub_local_callback_t callback,
                                        void *callback_context)
{
    fake_server_backend_state_t *state = context;
    (void)topic_hash;
    if (state->callback != callback || state->callback_context != callback_context)
    {
        return ROBUSTO_PROXY_STATUS_INTERNAL;
    }
    if (state->unsubscribe_status != ROBUSTO_PROXY_STATUS_OK)
    {
        return state->unsubscribe_status;
    }
    state->callback = NULL;
    state->callback_context = NULL;
    state->unsubscribe_calls += 1U;
    return ROBUSTO_PROXY_STATUS_OK;
}

static void test_pubsub_server_adapter_subscription_delivery_and_overflow(void)
{
    static const uint8_t topic[] = "sensor.temp";
    static const uint8_t second_topic[] = "sensor.pressure";
    static const uint8_t first_data[] = {0x19U, 0x2AU};
    static const uint8_t second_data[] = {0x30U, 0x31U, 0x32U};
    static uint8_t large_data[5000U];
    const robusto_proxy_pubsub_backend_t backend = {
        fake_server_publish, fake_server_subscribe, fake_server_unsubscribe};
    fake_server_backend_state_t backend_state = {0};
    robusto_proxy_pubsub_server_adapter_t adapter;
    robusto_proxy_pubsub_subscription_t subscriptions[1];
    uint8_t event_pool[4];
    robusto_proxy_pubsub_subscribe_request_t subscribe = {
        1U, topic, 11U, ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES};
    robusto_proxy_pubsub_subscribe_response_t subscribe_response;
    robusto_proxy_pubsub_publish_request_t publish = {
        2U, topic, 11U, first_data, sizeof(first_data)};
    robusto_proxy_pubsub_publish_response_t publish_response;
    robusto_proxy_pubsub_unsubscribe_request_t unsubscribe;
    robusto_proxy_pubsub_unsubscribe_response_t unsubscribe_response;
    robusto_proxy_pubsub_status_response_t status_response;
    robusto_proxy_pubsub_delivery_t delivery;
    robusto_proxy_service_t service;
    robusto_proxy_frame_header_t event_header;
    uint8_t payload[ROBUSTO_PROXY_MAX_PAYLOAD_BYTES];
    uint8_t event_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t delivery_opcode = 0U;
    size_t payload_size = 0U;
    size_t event_frame_size = 0U;
    const robusto_proxy_pubsub_adapter_t *operations;

    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_init(
        &adapter, &backend, &backend_state, subscriptions, 1U,
        event_pool, sizeof(event_pool), (robusto_proxy_pubsub_lock_t){0}));
    operations = robusto_proxy_pubsub_server_adapter_operations();
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->subscribe(&adapter, &subscribe, &subscribe_response));
    TEST_ASSERT_EQUAL_U32(1U, subscribe_response.subscription_id);
    TEST_ASSERT_EQUAL_U32(1U, subscribe_response.created);
    TEST_ASSERT_EQUAL_U32(1U, backend_state.subscribe_calls);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->subscribe(&adapter, &subscribe, &subscribe_response));
    TEST_ASSERT_EQUAL_U32(0U, subscribe_response.created);
    TEST_ASSERT_EQUAL_U32(1U, backend_state.subscribe_calls);
    subscribe.operation_id = 2U;
    subscribe.topic = second_topic;
    subscribe.topic_length = 15U;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_LIMIT,
                          operations->subscribe(&adapter, &subscribe, &subscribe_response));

    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->publish(&adapter, &publish, &publish_response));
    TEST_ASSERT_EQUAL_U32(1U, publish_response.delivery_count);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          backend_state.callback(backend_state.callback_context,
                                                 second_data, sizeof(second_data)));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY, delivery_opcode);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery(payload, payload_size, &delivery));
    TEST_ASSERT_EQUAL_U32(1U, delivery.subscription_id);
    TEST_ASSERT_EQUAL_U32(1U, delivery.delivery_sequence);
    TEST_ASSERT_TRUE(memcmp(delivery.data, first_data, sizeof(first_data)) == 0);
    robusto_proxy_service_init(&service, ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
                               1U, 1U, 9U, 2U, 0U);
    service.session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
    service.session.enabled_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_service_build_pubsub_delivery_event(
                              &service, payload, payload_size, event_frame,
                              sizeof(event_frame), &event_frame_size));
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_frame_validate_buffer(
                              event_frame, event_frame_size, NULL));
    memcpy(&event_header, event_frame, sizeof(event_header));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_FLAG_EVENT, event_header.flags);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_DOMAIN_PUBSUB, event_header.domain);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY, event_header.opcode);
    TEST_ASSERT_EQUAL_U32(0U, event_header.correlation_id);
    TEST_ASSERT_EQUAL_U32(9U, event_header.sequence);
    TEST_ASSERT_EQUAL_U32(1U, service.events);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          backend_state.callback(backend_state.callback_context,
                                                 second_data, sizeof(second_data)));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY, delivery_opcode);
    TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                          robusto_proxy_pubsub_decode_delivery(payload, payload_size, &delivery));
    TEST_ASSERT_EQUAL_U32(3U, delivery.delivery_sequence);

    for (size_t index = 0U; index < sizeof(large_data); ++index)
    {
        large_data[index] = (uint8_t)index;
    }
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          backend_state.callback(backend_state.callback_context,
                                                 large_data, sizeof(large_data)));
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_BEGIN, delivery_opcode);
    {
        robusto_proxy_pubsub_delivery_begin_t begin;
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_decode_delivery_begin(
                                  payload, payload_size, &begin));
        TEST_ASSERT_EQUAL_U32(5000U, begin.data_length);
        TEST_ASSERT_EQUAL_U32(4U, begin.delivery_sequence);
    }
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK, delivery_opcode);
    {
        robusto_proxy_pubsub_delivery_chunk_t chunk;
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_decode_delivery_chunk(
                                  payload, payload_size, &chunk));
        TEST_ASSERT_EQUAL_U32(0U, chunk.offset);
        TEST_ASSERT_EQUAL_U32(4080U, chunk.data_length);
        TEST_ASSERT_TRUE(memcmp(chunk.data, large_data, chunk.data_length) == 0);
    }
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_CHUNK, delivery_opcode);
    {
        robusto_proxy_pubsub_delivery_chunk_t chunk;
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_decode_delivery_chunk(
                                  payload, payload_size, &chunk));
        TEST_ASSERT_EQUAL_U32(4080U, chunk.offset);
        TEST_ASSERT_EQUAL_U32(920U, chunk.data_length);
        TEST_ASSERT_TRUE(memcmp(chunk.data, large_data + chunk.offset,
                                chunk.data_length) == 0);
    }
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_take_delivery(
        &adapter, true, &delivery_opcode, payload, sizeof(payload), &payload_size));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY_COMMIT, delivery_opcode);
    {
        robusto_proxy_pubsub_delivery_commit_t commit;
        TEST_ASSERT_EQUAL_INT(ROBUSTO_PROXY_RESULT_OK,
                              robusto_proxy_pubsub_decode_delivery_commit(
                                  payload, payload_size, &commit));
        TEST_ASSERT_EQUAL_U32(4U, commit.delivery_sequence);
    }

    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->status(&adapter, &status_response));
    TEST_ASSERT_EQUAL_U32(1U, status_response.active_subscriptions);
    TEST_ASSERT_EQUAL_U32(3U, status_response.delivery_events);
    TEST_ASSERT_EQUAL_U32(1U, status_response.delivery_drops);
    TEST_ASSERT_EQUAL_U32(1U, status_response.duplicate_operations);
    TEST_ASSERT_EQUAL_U32(1U, status_response.pubsub_errors);

    backend_state.publish_status = ROBUSTO_PROXY_STATUS_INTERNAL;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_INTERNAL,
                          operations->publish(&adapter, &publish, &publish_response));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->status(&adapter, &status_response));
    TEST_ASSERT_EQUAL_U32(2U, status_response.pubsub_errors);

    unsubscribe.operation_id = 3U;
    unsubscribe.subscription_id = subscribe_response.subscription_id;
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->unsubscribe(&adapter, &unsubscribe, &unsubscribe_response));
    TEST_ASSERT_EQUAL_U32(1U, unsubscribe_response.removed);
    TEST_ASSERT_EQUAL_U32(1U, backend_state.unsubscribe_calls);
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->unsubscribe(&adapter, &unsubscribe, &unsubscribe_response));
    TEST_ASSERT_EQUAL_U32(0U, unsubscribe_response.removed);
}

static void test_pubsub_server_adapter_deinit_retries_backend_failure(void)
{
    static const uint8_t topic[] = "sensor.temp";
    const robusto_proxy_pubsub_backend_t backend = {
        fake_server_publish, fake_server_subscribe, fake_server_unsubscribe};
    fake_server_backend_state_t backend_state = {0};
    robusto_proxy_pubsub_server_adapter_t adapter;
    robusto_proxy_pubsub_subscription_t subscriptions[1];
    robusto_proxy_pubsub_subscribe_request_t subscribe = {
        1U, topic, 11U, ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_DELIVERIES};
    robusto_proxy_pubsub_subscribe_response_t response;
    uint8_t event_pool[4];
    const robusto_proxy_pubsub_adapter_t *operations =
        robusto_proxy_pubsub_server_adapter_operations();

    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_init(
        &adapter, &backend, &backend_state, subscriptions, 1U,
        event_pool, sizeof(event_pool), (robusto_proxy_pubsub_lock_t){0}));
    TEST_ASSERT_EQUAL_U32(ROBUSTO_PROXY_STATUS_OK,
                          operations->subscribe(&adapter, &subscribe, &response));
    TEST_ASSERT_EQUAL_U32(1U, adapter.active_subscriptions);

    backend_state.unsubscribe_status = ROBUSTO_PROXY_STATUS_INTERNAL;
    TEST_ASSERT_FALSE(robusto_proxy_pubsub_server_adapter_deinit(&adapter));
    TEST_ASSERT_EQUAL_U32(1U, adapter.active_subscriptions);
    TEST_ASSERT_TRUE(backend_state.callback != NULL);

    backend_state.unsubscribe_status = ROBUSTO_PROXY_STATUS_OK;
    TEST_ASSERT_TRUE(robusto_proxy_pubsub_server_adapter_deinit(&adapter));
    TEST_ASSERT_EQUAL_U32(0U, adapter.active_subscriptions);
    TEST_ASSERT_TRUE(backend_state.callback == NULL);
    TEST_ASSERT_EQUAL_U32(1U, backend_state.unsubscribe_calls);
}

int main(void)
{
    test_crc32_golden_vector();
    test_header_validation_success_and_failures();
    test_frame_buffer_validation();
    test_frame_encode_round_trip();
    test_slot_empty_detection();
    test_profile_limits();
    test_session_seed_and_rollover_behavior();
    test_hello_response_clamping_and_rejection();
    test_control_payload_round_trip();
    test_control_payload_rejects_short_buffers();
    test_response_prefix_round_trip_and_validation();
    test_prefixed_control_response_messages();
    test_inflight_admission_lookup_completion_and_invalidation();
    test_inflight_rejects_invalid_arguments_and_duplicate_correlation();
    test_inflight_expiry_boundaries();
    test_session_degrades_on_timeout_expiry_signal();
    test_service_tick_and_health_simulation_timeout_path();
    test_service_health_happy_path_copies_counters();
    test_service_wraparound_uptime_and_timeout_expiry();
    test_service_mixed_inflight_expiry_only_expires_due_entries();
    test_service_health_wire_round_trip_from_builder();
    test_service_control_health_request_handler_happy_path();
    test_service_control_capability_query_paths();
    test_service_control_frame_dispatch_happy_path_and_bad_crc();
    test_service_hello_negotiation_and_rejection_paths();
    test_service_control_handler_rejects_unsupported_opcode();
    test_pubsub_request_golden_vectors();
    test_pubsub_response_and_delivery_round_trips();
    test_pubsub_codec_rejects_malformed_payloads();
    test_service_pubsub_dispatch_and_gates();
    test_pubsub_server_adapter_subscription_delivery_and_overflow();
    test_pubsub_server_adapter_chunked_publish();
    test_pubsub_server_adapter_deinit_retries_backend_failure();
    test_proxy_client_connect_publish_and_acceptance();

    if (tests_failed != 0)
    {
        fprintf(stderr, "step1 contract tests failed: %d of %d assertions\n", tests_failed, tests_run);
        return EXIT_FAILURE;
    }

    printf("step1 contract tests passed: %d assertions\n", tests_run);
    return EXIT_SUCCESS;
}
