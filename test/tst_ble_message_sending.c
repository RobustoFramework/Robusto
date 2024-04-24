#include "tst_ble_message_sending.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include <unity.h>
#include "tst_defs.h"

#include <string.h>

#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_concurrency.h>



static robusto_peer_t *remote_peer;

void ble_init_defs() {

    remote_peer = robusto_add_init_new_peer("TEST_BLE", kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR), robusto_mt_ble);

    remote_peer->protocol_version = 0;
    remote_peer->relation_id_outgoing =	TST_RELATIONID_01;
    remote_peer->peer_handle = 0;
    
}


void tst_ble_message_send_presentation(char * dest) {

	// Send it.
	add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR), robusto_mt_ble);
}

void tst_ble_message_send_message(void)
{
	r_delay(1000);

	ROB_LOGI("TEST", "In tst_ble_message_send_message.");

	robusto_peer_t * peer = robusto_peers_find_peer_by_base_mac_address(kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR));
	TEST_ASSERT_NOT_NULL_MESSAGE(peer, "Did not find the peer, did the presentation fail?");
    queue_state *state = robusto_malloc(sizeof(queue_state));
	rob_ret_val_t ret_val = send_message_strings(peer, 0, 0, &tst_strings, sizeof(tst_strings), state);
    TEST_ASSERT_MESSAGE(ret_val == ROB_OK ,"Bad result from send message.");
    rob_ret_val_t ret_val_flag;
    if (!robusto_waitfor_queue_state(state, 6000, &ret_val_flag)) {
        ROB_LOGE("TEST", "Failed because %i", ret_val_flag);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }
    robusto_free_queue_state(state);
	
}



void tst_ble_message_send_message_fragmented(void)
{
	r_delay(2000);

	ROB_LOGI("TEST", "In tst_ble_message_send_message (fragmented).");

	robusto_peer_t * peer = robusto_peers_find_peer_by_base_mac_address(kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR));
	TEST_ASSERT_NOT_NULL_MESSAGE(peer, "Did not find the peer, did the presentation fail?");
    queue_state *state = robusto_malloc(sizeof(queue_state));
    uint8_t * test_data = robusto_malloc(CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE);
    memset(test_data, 1, CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE);
	rob_ret_val_t ret_val = send_message_binary(peer, 0, 0,test_data, CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE, state);
    TEST_ASSERT_MESSAGE(ret_val == ROB_OK ,"Bad result from send message (fragmented).");
    rob_ret_val_t ret_val_flag;
    if (!robusto_waitfor_queue_state(state, CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE, &ret_val_flag)) {
        ROB_LOGE("TEST", "Failed because %i", ret_val_flag);
        TEST_FAIL_MESSAGE("Test fragmented failed, flag timed out or operation failed.");
    }
    robusto_free_queue_state(state);
	
}


#endif