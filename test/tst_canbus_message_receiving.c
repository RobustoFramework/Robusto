#include "tst_canbus_message_receiving.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#include <unity.h>

#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_incoming.h>
#include <robusto_concurrency.h>
#include <robusto_time.h>
#include "tst_defs.h"

static bool async_receive_flag = false;
static robusto_message_t *incoming_message = NULL;

static robusto_peer_t *incoming_peer = NULL;
static uint8_t *reply_msg = NULL;
static uint16_t reply_msg_length = 0;


void tst_canbus_message_receive_string_message_sync(void)
{
    ROB_LOGI("TEST", "In tst_canbus_message_receive_string_message");
    // TODO: Handle inverse leds (add robusto_led_solid)
    if (CONFIG_ROBUSTO_CANBUS_ADDRESS == 3) {
        robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 0);
    } else {
        robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 1);
    }
    
    robusto_message_t *message = NULL;
    int32_t startime = r_millis();
    rob_ret_val_t retval = ROB_INFO_RECV_NO_MESSAGE;
    int attempts = 0;
    while ((r_millis() < startime + 120000) && (retval == ROB_INFO_RECV_NO_MESSAGE)) {
        retval = robusto_receive_message_media_type(robusto_mt_canbus, &message);
        if ((retval == ROB_INFO_RECV_NO_MESSAGE) && (message != NULL)) {
            robusto_free(message->strings);
            robusto_free(message->raw_data);
            robusto_free(message);
        }
        //ROB_LOGI("-------", "Attempt %i", attempts);
        attempts++;
        r_delay(10);
    }
    if (CONFIG_ROBUSTO_CANBUS_ADDRESS == 3) {
        robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 1);
    } else {
        robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 0);
    }

    
    ROB_LOGI("TEST", "Receive attempts: %i ", attempts);

    TEST_ASSERT_EQUAL_MESSAGE(ROB_OK,retval, "Receive message did not return ROB_OK (-203 is no message).");
    if (retval == ROB_OK) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, message->string_count, "The string count is wrong.");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, message->strings[0], "First string did not match");
        TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), message->strings[1], "Second string did not match");
    }
    robusto_free(message->strings);
    robusto_free(message->raw_data);
    robusto_free(message);


}


bool canbus_tst_on_new_peer(robusto_peer_t * _peer) {
    ROB_LOGI("TEST", "In on_new_peer");
    incoming_peer = _peer;
    async_receive_flag = true;   
    return true;  
}

void tst_canbus_message_receive_presentation(void)
{
    ROB_LOGI("Test", "In tst_canbus_message_receive_presentation");
    #ifndef CONFIG_ROB_NETWORK_TEST_CANBUS_LOOP_INITIATOR 
    // We began, so we already know who the other peer is (as we added it)
    // Register the on work callback
    robusto_register_on_new_peer(&canbus_tst_on_new_peer);
    async_receive_flag = false;
    incoming_peer = NULL;

    if (robusto_waitfor_bool(&async_receive_flag, 40000)) {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_peer == NULL) {
        TEST_FAIL_MESSAGE("Test failed, incoming_peer NULL.");
    }
    #else 
    robusto_peer_t * peer = robusto_peers_find_peer_by_canbus_address(CONFIG_ROB_NETWORK_TEST_CANBUS_CALL_ADDR);
    TEST_ASSERT_NOT_NULL_MESSAGE(peer, "Did not find the peer, did we not succeed in adding it earlier?");
   
    if ((peer) && (!robusto_waitfor_byte(&(peer->state), PEER_KNOWN_INSECURE, 10000))) {
        TEST_FAIL_MESSAGE("The peer did not reach PEER_KNOWN_INSECURE state");
    }
	#endif
    // TODO: We need more tests here. I think.
    #if 0
    
    TEST_ASSERT_EQUAL_STRING_MESSAGE(incoming_peer->name, get_host_peer()->name, "The host name of the informed peer doesn't match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(incoming_peer->base_mac_address,  get_host_peer()->base_mac_address,  ROBUSTO_MAC_ADDR_LEN,
        "The base_mac_address doesn't match");    

    /* Check the response (offset one because of signal byte)*/
    uint8_t offset = 5;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x42, reply_msg[offset], "The message type doesn't match (should be MSG_NETWORK + Binary bit set)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NET_HIR, reply_msg[offset + 1], "The request type doesn't match (should be NET_HIR)");    
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ROBUSTO_PROTOCOL_VERSION, reply_msg[offset + 2], "The protocol version doesn't match");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ROBUSTO_PROTOCOL_VERSION_MIN, reply_msg[offset + 3], "The minimum protocol version doesn't match");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(get_host_supported_media_types(),reply_msg[offset + 4], "The supported media types doesn't match");
    #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(CONFIG_CANBUS_ADDR, reply_msg[offset + 5], "The CANBUS version doesn't match");
    #else
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, reply_msg[offset + 5], "The CANBUS version doesn't match");
    #endif
    
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(get_host_peer()->base_mac_address, reply_msg + offset + 10, ROBUSTO_MAC_ADDR_LEN, "The mac adress doesn't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(calc_relation_id(get_host_peer()->base_mac_address, reply_msg + offset + 10), *(uint32_t *)(reply_msg + offset + 6), "The relationid doesn't match");


    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&(get_host_peer()->name), 
        reply_msg + offset + 10 + ROBUSTO_MAC_ADDR_LEN,  
        reply_msg_length -(offset + 10 + ROBUSTO_MAC_ADDR_LEN),
        "The name doesn't match");

    rob_ret_val_t retval;
    if (!robusto_waitfor_flag(reply_msg, 500, &retval)) {
        ROB_LOGE("TEST", "Failed because %i", retval);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }
	#endif

}



void canbus_tst_do_on_work(incoming_queue_item_t *_incoming_item) {
    ROB_LOGI("TEST", "In canbus_tst_do_on_work, raw_length:%lu", _incoming_item->message->raw_data_length );
    _incoming_item->recipient_frees_message = true;
    incoming_message = _incoming_item->message;
    async_receive_flag = true;   
}

void tst_canbus_message_receive_string_message(void) {

    // Register the on work callback
    robusto_register_handler(canbus_tst_do_on_work);
    async_receive_flag = false;
    incoming_message = NULL;	

    if (robusto_waitfor_bool(&async_receive_flag, 30000)) {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_message == NULL) {
        TEST_FAIL_MESSAGE("Test failed, incoming_message NULL.");
    }
    //rob_log_bit_mesh(ROB_LOG_INFO, "TESTTEST", incoming_message->raw_data, incoming_message->raw_data_length);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, incoming_message->string_count, "The string count is wrong.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, incoming_message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), incoming_message->strings[1], "Second string did not match");
    robusto_message_free(incoming_message);

}

#endif

