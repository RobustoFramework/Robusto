#include "tst_i2c_message_sending.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include <unity.h>
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif
#include "tst_defs.h"

#include <string.h>

#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_concurrency.h>



static robusto_peer_t *remote_peer;

void i2c_init_defs() {

    
    remote_peer = robusto_malloc(sizeof(robusto_peer_t));
    remote_peer->protocol_version = 0;
    remote_peer->relation_id_outgoing =	TST_RELATIONID_01;
    remote_peer->peer_handle = 0;
    remote_peer->i2c_address = CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR;
    remote_peer->supported_media_types = robusto_mt_i2c;
    remote_peer->state = PEER_KNOWN_INSECURE;
    memcpy(remote_peer->name, TST_PEERNAME_0, sizeof(TST_PEERNAME_0));
    strncpy((char *)(remote_peer->base_mac_address), ROBUSTO_MAC_ADDRESS_BASE, 6);
    

}


void tst_i2c_message_send_presentation(char * dest) {

	r_delay(1000);
	// Send it.
	add_peer_by_i2c_address(dest, CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR);
}


void tst_i2c_message_send_message(void)
{
    
	r_delay(4000);

	ROB_LOGI("TEST", "In tst_i2c_message_send_message.");

	robusto_peer_t * peer = robusto_peers_find_peer_by_i2c_address(CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR);
    TEST_ASSERT_NOT_NULL_MESSAGE(peer, "Did not find the peer, did the presentation fail?");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(peer->state, PEER_UNKNOWN, "Peer not properly initialized.");
    queue_state *state = robusto_malloc(sizeof(queue_state));
	rob_ret_val_t ret_val_flag = send_message_multi(peer, 0, 0, &tst_strings, sizeof(tst_strings), NULL, 0, state, robusto_mt_none);
    TEST_ASSERT_MESSAGE(ret_val_flag == ROB_OK, "send_message_strings failed");
    if (!robusto_waitfor_queue_state(state, 6000, &ret_val_flag)) {
        ROB_LOGE("TEST", "Failed because %i", ret_val_flag);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }
    robusto_free(state);
	
}


#endif