#include "tst_message_presentation_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <unity.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_incoming.h>
#include <robusto_concurrency.h>
#include <robusto_time.h>
static bool async_receive_flag = false;
static bool incoming_queue_barrier = false;

static robusto_peer_t *incoming_peer = NULL;
/* Not used in the mock currently */
/*
static uint8_t *reply_msg = NULL;
static uint16_t reply_msg_length = 0;
*/

bool on_new_peer(robusto_peer_t * _peer) {
    incoming_peer = _peer;
    async_receive_flag = true;   

    return false;  
}

static void presentation_barrier_cb(incoming_queue_item_t *incoming_item)
{
    (void)incoming_item;
    incoming_queue_barrier = true;
}

void tst_async_mock_presentation(void)
{   
    robusto_print_peers();
    // Register the on work callback
    robusto_register_on_new_peer(&on_new_peer);
    async_receive_flag = false;
    incoming_peer = NULL;
    // This makes the mock backend start put a HI-message on the incoming queue
    set_message_expectation(MMI_HI);

    if (robusto_waitfor_bool(&async_receive_flag, 1000)) {
        ROB_LOGI("Test", "tst_async_mock_presentation: Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (!incoming_peer) {
        TEST_FAIL_MESSAGE("Test failed, incoming_peer NULL.");
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(get_host_peer()->name, incoming_peer->name, "The host name of the informed peer doesn't match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(get_host_peer()->base_mac_address, incoming_peer->base_mac_address,  ROBUSTO_MAC_ADDR_LEN,
        "The base_mac_address doesn't match");

    incoming_queue_barrier = false;
    robusto_register_handler(&presentation_barrier_cb);
    set_message_expectation(MMI_STRINGS_ASYNC);
    TEST_ASSERT_TRUE_MESSAGE(robusto_waitfor_bool(&incoming_queue_barrier, 1000),
                             "Incoming queue did not drain after presentation");

    robusto_peers_delete_peer(incoming_peer->peer_handle);
    incoming_peer = NULL;
    robusto_register_on_new_peer(NULL);
    robusto_register_handler(NULL);
    robusto_print_peers();
}

#endif