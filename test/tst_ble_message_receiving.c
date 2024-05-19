#include "tst_ble_message_receiving.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include <unity.h>
#include "tst_defs.h"
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_time.h>
#include <robusto_incoming.h>

static bool async_receive_flag = false;
static robusto_message_t *incoming_message = NULL;

static robusto_peer_t *incoming_peer = NULL;
static uint8_t *reply_msg = NULL;
static uint16_t reply_msg_length = 0;

bool ble_tst_on_new_peer(robusto_peer_t *_peer)
{
    ROB_LOGI("TEST", "In on_new_peer");
    incoming_peer = _peer;
    async_receive_flag = true;
    return true;
}

void tst_ble_message_receive_presentation(void)
{
    ROB_LOGI("Test", "In tst_ble_message_receive_presentation");
    // Register the on work callback

    robusto_peer_t * peer = robusto_peers_find_peer_by_base_mac_address(kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR));
    if (peer) {
        TEST_PASS();
    }

    robusto_register_on_new_peer(&ble_tst_on_new_peer);
    async_receive_flag = false;
    incoming_peer = NULL;

    if (robusto_waitfor_bool(&async_receive_flag, 40000))
    {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    }
    else
    {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_peer == NULL)
    {
        TEST_FAIL_MESSAGE("Test failed, incoming_peer NULL.");
    }
    r_delay(1000);
    // TODO: We need more tests here. I think.
}

void ble_tst_do_on_work(incoming_queue_item_t *_incoming_item)
{
    ROB_LOGI("TEST", "In ble_tst_do_on_work");
    incoming_message = _incoming_item->message;
    async_receive_flag = true;
    _incoming_item->recipient_frees_message = true;
}

void tst_ble_message_receive_string_message(void)
{

    // Register the on work callback
    robusto_register_handler(ble_tst_do_on_work);
    async_receive_flag = false;
    incoming_message = NULL;

    if (robusto_waitfor_bool(&async_receive_flag, 30000))
    {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    }
    else
    {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_message == NULL)
    {
        TEST_FAIL_MESSAGE("Test failed, incoming_item NULL.");
    }
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, incoming_message->string_count, "The string count is wrong.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, incoming_message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), incoming_message->strings[1], "Second string did not match");
    robusto_message_free(incoming_message);
    // TODO: Rewrite all incoming items to incoming message
}

void tst_ble_message_receive_fragmented_message(void)
{

    // Register the on work callback
    robusto_register_handler(ble_tst_do_on_work);
    async_receive_flag = false;
    incoming_message = NULL;

    if (robusto_waitfor_bool(&async_receive_flag, CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE))
    {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    }
    else
    {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_message == NULL)
    {
        TEST_FAIL_MESSAGE("Test failed, incoming_item NULL.");
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(CONFIG_ROBUSTO_TESTING_FRAGMENT_MESSAGE_SIZE, incoming_message->binary_data_length, "The binary data length is wrong.");
    robusto_message_free(incoming_message);
}

#endif