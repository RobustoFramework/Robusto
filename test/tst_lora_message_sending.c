#include "tst_lora_message_sending.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <unity.h>
#include "tst_defs.h"

#include <string.h>

#include <robusto_message.h>
#include <robusto_logging.h>	
#include <robusto_system.h>
#include <robusto_concurrency.h>

static robusto_peer_t *remote_peer = NULL;

void lora_init_defs() {
	if(remote_peer != NULL) {
		return;
	}
	remote_peer = robusto_add_init_new_peer("TEST_LORA", kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR), robusto_mt_lora);

	remote_peer->protocol_version = 0;
	remote_peer->relation_id_outgoing =	TST_RELATIONID_01;
	remote_peer->peer_handle = 0;
	remote_peer->state = PEER_KNOWN_INSECURE; 
	// We use macs in all initial communication. 

	
}


void tst_lora_message_send_message_sync(void)
{
	lora_init_defs();
	uint8_t *tst_strings_msg;

	int tst_strings_length = robusto_make_strings_message(MSG_MESSAGE, 0, 0, (uint8_t *)&tst_strings, 8, &tst_strings_msg);
   // TODO: Fix above and enable content of send message media type.
	int32_t startime = r_millis();
	rob_ret_val_t retval = ROB_FAIL;
	/*
	int attempts = 0;
	while ((r_millis() < startime + 10000)) //&& (retval != ROB_OK)) 
	{
		*/
	robusto_led_blink(50,50,1);
	
	retval = send_message_raw_internal(remote_peer, robusto_mt_lora, tst_strings_msg, tst_strings_length, NULL, true, false, 0,0);
	/*
		attempts++;
	}
	ROB_LOGI("TEST", "Send attempts: %i, time: %lu ", attempts, startime - r_millis());  
	*/
	//rob_ret_val_t retval = robusto_send_message_media_type(remote_peer, tst_strings_res, tst_strings_length, robusto_mt_i2c, false);
	TEST_ASSERT_EQUAL_MESSAGE(ROB_OK, retval, "Not the right response, ie ROB_OK (0).");

  
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_strings_message result", tst_strings_res, tst_msg_length);

	robusto_free(tst_strings_msg);
}

void tst_lora_message_send_presentation(char * dest) {

	r_delay(1000);
	// Send it.
	add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR), robusto_mt_lora);


}

void tst_lora_message_send_message(void)
{
	r_delay(4000);

	ROB_LOGI("TEST", "In tst_lora_message_send_message.");

	robusto_peer_t * peer = robusto_peers_find_peer_by_base_mac_address(kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR));
	TEST_ASSERT_NOT_NULL_MESSAGE(peer, "Did not find the peer, did the presentation fail?");
    queue_state *state =  (uint8_t *)robusto_malloc(sizeof(queue_state));
	rob_ret_val_t retval = send_message_multi(peer, 0, 0, &tst_strings, sizeof(tst_strings), NULL, 0, state, robusto_mt_lora);
    rob_ret_val_t ret_val_flag;
    if (!robusto_waitfor_queue_state(state, 6000, &ret_val_flag)) {
        ROB_LOGE("TEST", "Failed because %i", ret_val_flag);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }
    robusto_free_queue_state(state);
	
}

// TODO: We'd need a receive message cycle here

#endif