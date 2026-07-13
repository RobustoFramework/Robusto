#include "tst_fragmentation.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
// TODO: Add copyrights on all these files
#include <unity.h>

#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_concurrency.h>
#include <robusto_time.h>
#include <robusto_init.h>
#include <robusto_peer.h>
#include <robusto_message.h>
#include <robusto_incoming.h>
#include <robusto_system.h>
#include "tst_defs.h"

#define TST_DATA_SIZE 1000
#define TST_FRAG_SIZE 200
#define TST_FRAG_HEADER_LEN (ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN + 1 + 4)
#define FRAG_TAG "fragmentation"
robusto_peer_t *peer = NULL;
robusto_message_t *message = NULL;
bool async_receive_flag = false;
uint32_t call_counter = 0;
bool skip_fragments = false;
uint8_t * test_data;
uint32_t frag_request_count = 0;
uint32_t frag_message_count = 0;
uint32_t zero_len_frag_message_count = 0;
uint32_t announced_fragment_count = 0;
static bool captured_request = false;
static uint32_t captured_request_fragment_count = 0;
static uint32_t sent_result_count = 0;
static uint32_t sent_resend_count = 0;

static uint64_t memory_loss_bytes(uint64_t before_mem, uint64_t after_mem)
{
    return (after_mem < before_mem) ? (before_mem - after_mem) : 0;
}

static void reset_fragment_tracking(void)
{
    frag_request_count = 0;
    frag_message_count = 0;
    zero_len_frag_message_count = 0;
    announced_fragment_count = 0;
    captured_request = false;
    captured_request_fragment_count = 0;
    sent_result_count = 0;
    sent_resend_count = 0;
}

static uint8_t *build_frag_request_packet(uint32_t receive_buffer_length, uint32_t fragment_count, uint32_t fragment_size, uint32_t hash)
{
    uint8_t *packet = robusto_malloc(ROBUSTO_CRC_LENGTH + 18);
    TEST_ASSERT_NOT_NULL_MESSAGE(packet, "Failed allocating FRAG_REQUEST packet");

    packet[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    packet[ROBUSTO_CRC_LENGTH + 1] = FRAG_REQUEST;
    memcpy(packet + ROBUSTO_CRC_LENGTH + 2, &receive_buffer_length, 4);
    memcpy(packet + ROBUSTO_CRC_LENGTH + 6, &fragment_count, 4);
    memcpy(packet + ROBUSTO_CRC_LENGTH + 10, &fragment_size, 4);
    memcpy(packet + ROBUSTO_CRC_LENGTH + 14, &hash, 4);

    uint32_t crc = robusto_crc32(0, packet + 4, 18);
    memcpy(packet, &crc, 4);
    return packet;
}

static uint8_t *build_frag_message_packet(uint32_t hash, uint32_t index, const uint8_t *payload, uint32_t payload_len)
{
    uint8_t *packet = robusto_malloc(TST_FRAG_HEADER_LEN + payload_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(packet, "Failed allocating FRAG_MESSAGE packet");

    memcpy(packet, &hash, 4);
    packet[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    packet[ROBUSTO_CRC_LENGTH + 1] = FRAG_MESSAGE;
    memcpy(packet + ROBUSTO_CRC_LENGTH + 2, &index, 4);
    if (payload_len > 0)
    {
        memcpy(packet + TST_FRAG_HEADER_LEN, payload, payload_len);
    }

    return packet;
}

static rob_ret_val_t callback_capture_frag_responses(robusto_peer_t *peer, uint8_t *data, uint32_t len, bool receipt)
{
    (void)peer;
    (void)len;
    (void)receipt;

    if (data[ROBUSTO_CRC_LENGTH] == MSG_FRAGMENTED)
    {
        if (data[ROBUSTO_CRC_LENGTH + 1] == FRAG_RESULT)
        {
            sent_result_count++;
        }
        if (data[ROBUSTO_CRC_LENGTH + 1] == FRAG_RESEND)
        {
            sent_resend_count++;
        }
    }

    return ROB_OK;
}

static rob_ret_val_t callback_capture_request_and_fail(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt)
{
    (void)peer;
    (void)receipt;

    if (len >= ROBUSTO_CRC_LENGTH + 10 && data[ROBUSTO_CRC_LENGTH] == MSG_FRAGMENTED && data[ROBUSTO_CRC_LENGTH + 1] == FRAG_REQUEST)
    {
        captured_request = true;
        memcpy(&captured_request_fragment_count, data + ROBUSTO_CRC_LENGTH + 6, sizeof(captured_request_fragment_count));
    }

    // Stop transmission right after metadata so we can unit-test request content only.
    return ROB_FAIL;
}

static robusto_peer_t *ensure_fragmentation_mock_peer(void)
{
    robusto_peer_t *local_peer = robusto_peers_find_peer_by_name("TEST_MOCK_1");
    if (!local_peer)
    {
        local_peer = robusto_add_init_new_peer("TEST_MOCK_1", (rob_mac_address *)kconfig_mac_to_6_bytes(11), robusto_mt_mock);
        TEST_ASSERT_NOT_NULL_MESSAGE(local_peer, "Failed creating TEST_MOCK_1 peer for fragmentation test");
        local_peer->protocol_version = 0;
        local_peer->relation_id_incoming = TST_RELATIONID_01;
        local_peer->peer_handle = 0;
    }

    return local_peer;
}

void cb_incoming(incoming_queue_item_t *incoming_item) {
    ROB_LOGI(FRAG_TAG,"cb_incoming");
    incoming_item->recipient_frees_message = true;
    message = incoming_item->message;
    async_receive_flag = true;
}

rob_ret_val_t callback_response_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {

    ROB_LOGI(FRAG_TAG,"Got response, call %lu", call_counter);
    //rob_log_bit_mesh(ROB_LOG_INFO, FRAG_TAG, data, len);
    handle_fragmented(peer, robusto_mt_mock, data, len, TST_FRAG_SIZE, (cb_send_message *)&callback_response_message);
    call_counter++;
    return ROB_OK;
}

rob_ret_val_t callback_send_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {
    ROB_LOGI(FRAG_TAG,"callback_send_message: call %lu", call_counter);

    if (len >= ROBUSTO_CRC_LENGTH + 2 && data[ROBUSTO_CRC_LENGTH] == MSG_FRAGMENTED)
    {
        uint8_t frag_type = data[ROBUSTO_CRC_LENGTH + 1];
        if (frag_type == FRAG_REQUEST && len >= ROBUSTO_CRC_LENGTH + 18)
        {
            frag_request_count++;
            memcpy(&announced_fragment_count, data + ROBUSTO_CRC_LENGTH + 6, sizeof(announced_fragment_count));
        }
        else if (frag_type == FRAG_MESSAGE)
        {
            frag_message_count++;
            if (len == TST_FRAG_HEADER_LEN)
            {
                zero_len_frag_message_count++;
            }
        }
    }

    if (skip_fragments && (call_counter == 2 || call_counter == 4)) {
        ROB_LOGI(FRAG_TAG,"Skipping call %lu", call_counter);
    } else {
        // We need to copy here so we can free the data on both ends as normal.
        uint8_t * tmp_data = robusto_malloc(len);
        memcpy(tmp_data, data, len);
        handle_fragmented(peer, robusto_mt_mock, tmp_data, len, TST_FRAG_SIZE, (cb_send_message *)&callback_response_message);
    }
    
    call_counter ++;
    return ROB_OK;
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void fake_message() {

    robusto_register_handler(&cb_incoming);
    peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_MESSAGE(peer, "tst_fragmentation_complete TEST_MOCK_1 peer is not set");

    // Build a 10K message
    test_data = robusto_malloc(TST_DATA_SIZE);
    memset(test_data, 0, 200);
    memset(test_data + 200, 1, 200);
    memset(test_data + 400, 2, 200);
    memset(test_data + 600, 3, 200);
    memset(test_data + 800, 4, 200);
    uint8_t * msg;

    uint32_t msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 0, 0, test_data, TST_DATA_SIZE, NULL, 0, &msg);
    call_counter = 0;
    async_receive_flag = false;
    rob_ret_val_t res = send_message_fragmented(peer, robusto_mt_mock, msg  + ROBUSTO_PREFIX_BYTES, msg_length - ROBUSTO_PREFIX_BYTES, TST_FRAG_SIZE, (cb_send_message *)&callback_send_message);
    ROB_LOGI("Test", "tst_fragmentation_complete");
   if (robusto_waitfor_bool(&async_receive_flag, 10000)) {
        ROB_LOGI("Test", "tst_fragmentation_complete: Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (message == NULL) {
        TEST_FAIL_MESSAGE("Test failed, incoming_item NULL.");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(TST_DATA_SIZE, message->binary_data_length, "The length of the binary data is wrong.");
    robusto_message_free(message);
}

void fake_message_sized(uint32_t test_data_size)
{
    robusto_register_handler(&cb_incoming);
    peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_MESSAGE(peer, "tst_fragmentation_complete TEST_MOCK_1 peer is not set");

    test_data = robusto_malloc(test_data_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(test_data, "Failed to allocate test data");
    for (uint32_t i = 0; i < test_data_size; i++)
    {
        test_data[i] = (uint8_t)(i % 251);
    }

    uint8_t *msg = NULL;
    uint32_t msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 0, 0, test_data, test_data_size, NULL, 0, &msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(msg, "Failed to build robusto message");

    call_counter = 0;
    async_receive_flag = false;
    reset_fragment_tracking();

    rob_ret_val_t res = send_message_fragmented(peer, robusto_mt_mock, msg + ROBUSTO_PREFIX_BYTES, msg_length - ROBUSTO_PREFIX_BYTES,
                                                TST_FRAG_SIZE, (cb_send_message *)&callback_send_message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_OK, res, "Fragmented send should complete in mock loopback");

    if (robusto_waitfor_bool(&async_receive_flag, 10000))
    {
        ROB_LOGI("Test", "fake_message_sized: Async receive flag was set to true.");
    }
    else
    {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }

    TEST_ASSERT_NOT_NULL_MESSAGE(message, "Test failed, incoming_item NULL.");
    TEST_ASSERT_EQUAL_INT_MESSAGE(test_data_size, message->binary_data_length, "The length of the binary data is wrong.");

    robusto_message_free(message);
    message = NULL;
    robusto_free(msg);
    robusto_free(test_data);
    test_data = NULL;
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void tst_fragmentation_complete(void) {
    ROB_LOGI("fragmentation", "in tst_fragmentation_complete");
    skip_fragments = false;
    fake_message();

}

/**
 * @brief Fake a fragmented transmission, resending missing parts
 */
void tst_fragmentation_resending(void) {
    ROB_LOGI("fragmentation", "in tst_fragmentation_resending");
    skip_fragments = true;
    fake_message();

}

void tst_fragmentation_even_division_fragment_metadata(void)
{
    ROB_LOGI("fragmentation", "in tst_fragmentation_even_division_fragment_metadata");
    skip_fragments = false;

    fake_message_sized(1000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, frag_request_count, "Expected exactly one fragment request");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, announced_fragment_count,
                                     "Evenly divisible payload should announce exactly 5 fragments");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, frag_message_count,
                                     "Evenly divisible payload should emit exactly 5 FRAG_MESSAGE packets");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, zero_len_frag_message_count,
                                     "Evenly divisible payload must not emit zero-length fragments");
}

void tst_fragmentation_non_division_fragment_metadata(void)
{
    ROB_LOGI("fragmentation", "in tst_fragmentation_non_division_fragment_metadata");
    skip_fragments = false;

    fake_message_sized(1001);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, frag_request_count, "Expected exactly one fragment request");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, announced_fragment_count,
                                     "Non-divisible payload should announce ceil(payload/frag) fragments");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, frag_message_count,
                                     "Non-divisible payload should emit exactly 6 FRAG_MESSAGE packets");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, zero_len_frag_message_count,
                                     "Non-divisible payload must not emit zero-length fragments");
}

void tst_fragmentation_request_even_division_count(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    uint8_t *data = robusto_malloc(1000);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, 0xAA, 1000);

    reset_fragment_tracking();
    rob_ret_val_t res = send_message_fragmented(local_peer, robusto_mt_mock, data, 1000, TST_FRAG_SIZE,
                                                (cb_send_message *)&callback_capture_request_and_fail);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_FAIL, res, "Test callback should abort after capturing FRAG_REQUEST");
    TEST_ASSERT_TRUE_MESSAGE(captured_request, "Expected to capture FRAG_REQUEST metadata");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, captured_request_fragment_count,
                                     "Evenly divisible payload must announce exactly 5 fragments");
    robusto_free(data);
}

void tst_fragmentation_request_non_division_count(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    uint8_t *data = robusto_malloc(1001);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, 0xBB, 1001);

    reset_fragment_tracking();
    rob_ret_val_t res = send_message_fragmented(local_peer, robusto_mt_mock, data, 1001, TST_FRAG_SIZE,
                                                (cb_send_message *)&callback_capture_request_and_fail);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROB_FAIL, res, "Test callback should abort after capturing FRAG_REQUEST");
    TEST_ASSERT_TRUE_MESSAGE(captured_request, "Expected to capture FRAG_REQUEST metadata");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, captured_request_fragment_count,
                                     "Non-divisible payload must announce ceil(payload/frag) fragments");
    robusto_free(data);
}

void tst_fragmentation_crc_mismatch_cleans_up_state(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    reset_fragment_tracking();

    // Deliberately wrong hash to force CRC mismatch after assembly.
    uint32_t wrong_hash = 0x12345678;
    uint8_t *request = build_frag_request_packet(2, 2, 1, wrong_hash);
    bool req_receipt = handle_fragmented(local_peer, robusto_mt_mock, request, ROBUSTO_CRC_LENGTH + 18,
                                         TST_FRAG_SIZE, &callback_capture_frag_responses);
    TEST_ASSERT_TRUE_MESSAGE(req_receipt, "FRAG_REQUEST should request receipt");

    uint8_t p0[1] = {0xAA};
    uint8_t p1[1] = {0xBB};
    uint8_t *m0 = build_frag_message_packet(wrong_hash, 0, p0, 1);
    uint8_t *m1 = build_frag_message_packet(wrong_hash, 1, p1, 1);

    handle_fragmented(local_peer, robusto_mt_mock, m0, TST_FRAG_HEADER_LEN + 1, TST_FRAG_SIZE,
                      &callback_capture_frag_responses);
    handle_fragmented(local_peer, robusto_mt_mock, m1, TST_FRAG_HEADER_LEN + 1, TST_FRAG_SIZE,
                      &callback_capture_frag_responses);

    TEST_ASSERT_TRUE_MESSAGE(sent_result_count > 0, "Expected FRAG_RESULT to be sent on hash mismatch");
    TEST_ASSERT_NULL_MESSAGE(get_last_frag_message(),
                             "Fragment state should be cleaned up after CRC mismatch result");
}

void tst_fragmentation_missing_fragments_does_not_leak_memory(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    reset_fragment_tracking();

    uint8_t payload[2] = {0x11, 0x22};
    uint32_t hash = robusto_crc32(0, payload, 2);
    uint8_t *request = build_frag_request_packet(2, 2, 1, hash);
    bool req_receipt = handle_fragmented(local_peer, robusto_mt_mock, request, ROBUSTO_CRC_LENGTH + 18,
                                         TST_FRAG_SIZE, &callback_capture_frag_responses);
    TEST_ASSERT_TRUE(req_receipt);

    uint8_t p1[1] = {payload[1]};
    uint64_t before_mem = get_free_mem();

    for (int i = 0; i < 220; i++)
    {
        uint8_t *m1 = build_frag_message_packet(hash, 1, p1, 1);
        handle_fragmented(local_peer, robusto_mt_mock, m1, TST_FRAG_HEADER_LEN + 1, TST_FRAG_SIZE,
                          &callback_capture_frag_responses);
    }

    uint64_t after_mem = get_free_mem();
    TEST_ASSERT_TRUE_MESSAGE(sent_resend_count > 0, "Expected FRAG_RESEND responses when fragments are missing");
    // Should be approximately stable; leak in resend path currently accumulates allocations.
    TEST_ASSERT_TRUE_MESSAGE(memory_loss_bytes(before_mem, after_mem) < 256,
                             "Repeated FRAG_RESEND requests should not consume significant memory");
}

void tst_fragmentation_short_request_does_not_create_state(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    reset_fragment_tracking();
    fragmented_message_t *before = get_last_frag_message();

    // Packet is fully allocated to keep the test deterministic, but advertised length is intentionally too short.
    uint8_t payload[2] = {0x10, 0x20};
    uint32_t hash = robusto_crc32(0, payload, 2);
    uint8_t *request = build_frag_request_packet(2, 2, 1, hash);

    bool req_receipt = handle_fragmented(local_peer, robusto_mt_mock, request, ROBUSTO_CRC_LENGTH + 8,
                                         TST_FRAG_SIZE, &callback_capture_frag_responses);
    TEST_ASSERT_TRUE_MESSAGE(req_receipt, "FRAG_REQUEST should still map to receipt semantics");
    fragmented_message_t *after = get_last_frag_message();
    if (after != NULL)
    {
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(hash, after->hash,
                                             "Short FRAG_REQUEST must be rejected without creating fragmented state");
    }
    TEST_ASSERT_EQUAL_PTR_MESSAGE(before, after,
                                  "Short FRAG_REQUEST should not mutate active fragmented message state");
}

void tst_fragmentation_interleaved_hashes_are_resolved(void)
{
    robusto_peer_t *local_peer = ensure_fragmentation_mock_peer();
    TEST_ASSERT_NOT_NULL(local_peer);

    reset_fragment_tracking();

    uint8_t payload_a[2] = {0x31, 0x32};
    uint8_t payload_b[1] = {0x41};
    uint32_t hash_a = robusto_crc32(0, payload_a, 2);
    uint32_t hash_b = robusto_crc32(0, payload_b, 1);

    uint8_t *request_a = build_frag_request_packet(2, 2, 1, hash_a);
    uint8_t *request_b = build_frag_request_packet(1, 1, 1, hash_b);

    handle_fragmented(local_peer, robusto_mt_mock, request_a, ROBUSTO_CRC_LENGTH + 18,
                      TST_FRAG_SIZE, &callback_capture_frag_responses);
    handle_fragmented(local_peer, robusto_mt_mock, request_b, ROBUSTO_CRC_LENGTH + 18,
                      TST_FRAG_SIZE, &callback_capture_frag_responses);

    // Last fragment for hash_a should trigger FRAG_RESEND for missing fragment 0.
    uint8_t *a_last = build_frag_message_packet(hash_a, 1, payload_a + 1, 1);
    handle_fragmented(local_peer, robusto_mt_mock, a_last, TST_FRAG_HEADER_LEN + 1, TST_FRAG_SIZE,
                      &callback_capture_frag_responses);

    TEST_ASSERT_TRUE_MESSAGE(sent_resend_count > 0,
                             "Interleaved fragmented transmissions should resolve by hash and request missing parts");
}

#endif

