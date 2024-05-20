#include "tst_lora_message_receiving.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <unity.h>
#include "tst_defs.h"
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_time.h>
#include <robusto_incoming.h>
#include <robusto_concurrency.h>

static bool async_receive_flag = false;
static incoming_queue_item_t *incoming_item = NULL;

static robusto_peer_t *incoming_peer = NULL;
static uint8_t *reply_msg = NULL;
static uint16_t reply_msg_length = 0;



bool lora_tst_on_new_peer(robusto_peer_t * _peer) {
    ROB_LOGI("TEST", "In on_new_peer");
    incoming_peer = _peer;
    async_receive_flag = true;   
    return true;  
}

void tst_lora_message_receive_presentation(void)
{
    ROB_LOGI("Test", "In tst_lora_message_receive_presentation");
    // Register the on work callback
    robusto_register_on_new_peer(&lora_tst_on_new_peer);
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
    // TODO: We need more tests here. I think.
    

}


void lora_tst_do_on_work(incoming_queue_item_t *_incoming_item) {
    ROB_LOGI("TEST", "In lora_tst_do_on_work");
    incoming_item = _incoming_item;
    async_receive_flag = true;   
    #warning "We need to have incoming_message = _incoming_item->message and service freeing for this to work safely"
}

void tst_lora_message_receive_string_message(void) {

    // Register the on work callback
    robusto_register_handler(lora_tst_do_on_work);
    async_receive_flag = false;
    incoming_item = NULL;	

    if (robusto_waitfor_bool(&async_receive_flag, 30000)) {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_item == NULL) {
        TEST_FAIL_MESSAGE("Test failed, incoming_item NULL.");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, incoming_item->message->string_count, "The string count is wrong.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, incoming_item->message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), incoming_item->message->strings[1], "Second string did not match");
    

}
#endif
