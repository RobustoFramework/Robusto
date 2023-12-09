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

#define TST_DATA_SIZE 1000
#define TST_FRAG_SIZE 200
#define FRAG_TAG "fragmentation"
robusto_peer_t *peer = NULL;
robusto_message_t *message = NULL;
bool async_receive_flag = false;
uint32_t call_counter = 0;
bool skip_fragments = false;
uint8_t * test_data;

void cb_incoming(incoming_queue_item_t *incoming_item) {
    incoming_item->service_frees_message = true;
    message = incoming_item->message;
    async_receive_flag = true;
}

rob_ret_val_t callback_response_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {

    ROB_LOGI(FRAG_TAG,"Got response, call %lu", call_counter);
    //rob_log_bit_mesh(ROB_LOG_INFO, FRAG_TAG, data, len);
    handle_fragmented(peer, robusto_mt_mock, data, len, TST_FRAG_SIZE, &callback_response_message);
    call_counter++;
    return ROB_OK;
}

rob_ret_val_t callback_send_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {
    ROB_LOGI(FRAG_TAG,"callback_send_message: call %lu", call_counter);
    if (skip_fragments && (call_counter == 2 || call_counter == 4)) {
        ROB_LOGI(FRAG_TAG,"Skipping call %lu", call_counter);
    } else {
       handle_fragmented(peer, robusto_mt_mock, data, len, TST_FRAG_SIZE, &callback_response_message);
    }
    
    call_counter ++;
    return ROB_OK;
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void fake_message() {

    robusto_register_handler(&cb_incoming);
    peer = robusto_peers_find_peer_by_name("TEST_MOCK");
    TEST_ASSERT_MESSAGE(peer != NULL, "tst_fragmentation_complete TEST_MOCK is not set");

    // Build a 10K message
    test_data = robusto_malloc(TST_DATA_SIZE);
    memset(test_data, 0, 200);
    memset(test_data + 200, 1, 200);
    memset(test_data + 400, 2, 200);
    memset(test_data + 600, 3, 200);
    memset(test_data + 800, 4, 200);
    uint8_t * msg;

    uint32_t msg_length = robusto_make_binary_message(MSG_MESSAGE, 0, 0, test_data, TST_DATA_SIZE, &msg);
    call_counter = 0;
    async_receive_flag = false;
    rob_ret_val_t res = send_message_fragmented(peer, robusto_mt_mock, msg  + ROBUSTO_PREFIX_BYTES, msg_length - ROBUSTO_PREFIX_BYTES, TST_FRAG_SIZE, &callback_send_message);
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

#endif

