#include "tst_qos_sending.h"

#ifdef CONFIG_ROBUSTO_NETWORK_QOS_TESTING
#include <unity.h>
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

#include <string.h>
#include "tst_defs.h"
#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_concurrency.h>
#include <robusto_peer.h>
#include <robusto_qos.h>

static robusto_peer_t *r_peer;

void tst_qos_message_send_presentation(char * dest) {


	// Send it.
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_I2C
    r_peer = add_peer_by_i2c_address(dest, CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR);
    #endif
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_ESP_NOW
    r_peer = add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR), robusto_mt_espnow);
    #endif
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_LORA
    r_peer = add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR), robusto_mt_lora);
    #endif
    if (r_peer == NULL) {
        TEST_FAIL_MESSAGE("Failed adding the peer.");
    }
    // Wait for the state to change from 0 (PEER_UNKNOWN)
    if (!robusto_waitfor_byte(&(r_peer->state), PEER_KNOWN_INSECURE, 4000)) {
        TEST_FAIL_MESSAGE("The peer didn't reach PEER_KNOWN_INSECURE state.");
    } else {
        TEST_PASS();
    }	
}

void tst_qos_start_sending_data(void) {
    // This simulates problems with two medias.

    // First send a message

    if (r_peer == NULL) {
        TEST_FAIL_MESSAGE("tst_qos_start_sending_data: No peer.");
    }
    if (r_peer->state == PEER_UNKNOWN) {
        TEST_FAIL_MESSAGE("tst_qos_start_sending_data: Presentation seems previously failed.");
    }    
    queue_state *state = robusto_malloc(sizeof(queue_state));
    rob_ret_val_t ret_val =  send_message_strings(r_peer, 0,0, &tst_strings, 8, state);
    TEST_ASSERT_MESSAGE(ret_val == ROB_OK ,"Bad result from send message.");
    rob_ret_val_t ret_val_flag;
    if (!robusto_waitfor_queue_state(state, 6000, &ret_val_flag)) {
        ROB_LOGE("TEST", "Failed because %i", ret_val_flag);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }    
    robusto_free_queue_state(state);
    e_media_type orig_media_type = r_peer->last_selected_media_type;
    robusto_media_t * media_info_1 =  get_media_info(r_peer, orig_media_type);
    // Add lots of messages to queue while increasing failure rates (just two, not more is needed without history)
    float failure_rate = 0;

    for (int msg_counter = 0; msg_counter < 2; msg_counter++) {
        send_message_strings(r_peer, 0,0, &tst_strings, 8, NULL);
        failure_rate = add_to_failure_rate_history(media_info_1, 1);
        ROB_LOGI("TEST", "Sent %i times, FR = %f.", msg_counter +1, failure_rate);
    }
    
    TEST_ASSERT_NOT_EQUAL_MESSAGE(r_peer->last_selected_media_type, orig_media_type, "Media hasn't changed");
    robusto_media_t * media_info_2 =  get_media_info(r_peer, r_peer->last_selected_media_type);
     // Add lots of messages to queue while increasing failure rates
    float failure_rate_1 = 0;
    float failure_rate_2 = 0;
    for (int msg_counter = 0; msg_counter < 4; msg_counter++) {
        send_message_strings(r_peer, 0,0, &tst_strings, 8, NULL);
        failure_rate_1 = add_to_failure_rate_history(media_info_1, 0);
        failure_rate_2 = add_to_failure_rate_history(media_info_2, 1);
        ROB_LOGI("TEST", "Sent %i times, FR1 = %f, FR2 = %f.", msg_counter +1, failure_rate_1, failure_rate_2);
    }
    TEST_ASSERT_EQUAL_MESSAGE(r_peer->last_selected_media_type, orig_media_type, "Media hasn't changed back");
    
    
}

/*    
    RUN_TEST(tst_qos_start_sending_data);
    robusto_yield();
    
*/

#endif
