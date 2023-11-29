#include "tst_fragmentation.h"

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
uint32_t fragment_counter = 0;
bool skip_fragments = false;

void cb_incoming(incoming_queue_item_t *incoming_item) {
    incoming_item->service_frees_message = true;
    message = incoming_item->message;
    async_receive_flag = true;
}

rob_ret_val_t callback_response_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {

    ROB_LOGI(FRAG_TAG,"response");
    rob_log_bit_mesh(ROB_LOG_INFO, FRAG_TAG, data, len);
    return ROB_OK;
}

rob_ret_val_t callback_send_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt) {
    ROB_LOGI(FRAG_TAG,"Sending fragment %lu", fragment_counter);
    if (skip_fragments && (fragment_counter == 2 || fragment_counter == 4)) {
        ROB_LOGI(FRAG_TAG,"Skipping fragment %lu", fragment_counter);
    } else {
        handle_fragmented(peer, &(peer->mock_info), data, len, TST_FRAG_SIZE, &callback_response_message);
    }
    
    fragment_counter ++;
    return ROB_OK;
}

/**
 * @brief Fake a happy flow fragmented transmission
 */
void fake_message() {

    ROB_LOGI("fragmentation", "in tst_fragmentation_complete");
    robusto_register_handler(&cb_incoming);
    peer = robusto_peers_find_peer_by_name("TEST_MOCK");
    TEST_ASSERT_MESSAGE(peer != NULL, "tst_fragmentation_complete :TEST_MOCK is not set");


    // Build a 10K message
    uint8_t * data = robusto_malloc(TST_DATA_SIZE);
    memset(data, 1, TST_DATA_SIZE);
    uint8_t * msg;

    uint32_t msg_length = robusto_make_binary_message(MSG_MESSAGE, 0, 0, data, TST_DATA_SIZE, &msg);
    fragment_counter = 0;
    async_receive_flag = false;
    rob_ret_val_t res = send_message_fragmented(peer, &peer->mock_info, msg  + ROBUSTO_PREFIX_BYTES, msg_length - ROBUSTO_PREFIX_BYTES,  200, &callback_send_message);
    // TODO: After generalizing this functionality, perhaps always send a couple of fragments each time? Like 10?
    if (robusto_waitfor_bool(&async_receive_flag, 30000)) {
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
    skip_fragments = false;
    fake_message();

}

/**
 * @brief Fake a fragmented transmission, resending missing parts
 */
void tst_fragmentation_resending(void) {
    skip_fragments = true;
    fake_message();

}

