#include "tst_esp_now_message_receiving.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
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

void tst_esp_now_message_receive_string_message_sync(void)
{

    // TODO: Handle inverse leds (add robusto_led_solid)
    robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 1);

    robusto_message_t *message = NULL;
    int32_t startime = r_millis();
    rob_ret_val_t retval = ROB_INFO_RECV_NO_MESSAGE;
    int attempts = 0;
    while ((r_millis() < startime + 120000) && (retval == ROB_INFO_RECV_NO_MESSAGE))
    {
        retval = robusto_receive_message_media_type(robusto_mt_espnow, &message);
        if ((retval == ROB_INFO_RECV_NO_MESSAGE) && (message != NULL))
        {
            robusto_message_free(incoming_message);
        }
        attempts++;
        r_delay(10);
    }

    robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 0);

    ROB_LOGI("TEST", "Receive attempts: %i ", attempts);

    TEST_ASSERT_EQUAL_MESSAGE(ROB_OK, retval, "Receive message did not return ROB_OK (-203 is no message).");
    if (retval == ROB_OK)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, message->string_count, "The string count is wrong.");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, message->strings[0], "First string did not match");
        TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), message->strings[1], "Second string did not match");
    }
    robusto_message_free(incoming_message);
}

#if 0
void tst_message_receive_binary_message_mock(void)
{
    set_message_expectation(MMI_BINARY);
    robusto_message_t *message;
    rob_ret_val_t retval = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(retval == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, message->binary_data, message->binary_data_length, "Binary data did not match");
    robusto_free(message->raw_data);
    robusto_free(message);
}

void tst_message_receive_multi_message_mock(void)
{
    set_message_expectation(MMI_MULTI);
    robusto_message_t *message;
    rob_ret_val_t retval = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(retval == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, message->binary_data, message->binary_data_length, "Binary data did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), message->strings[1], "Second string did not match");
    robusto_free(message->strings);
    robusto_free(message->raw_data);
    robusto_free(message);
   

}

void tst_message_receive_binary_message_restricted_mock(void)
{
    set_message_expectation(MMI_BINARY_RESTRICTED);
    robusto_message_t *message;
    rob_ret_val_t retval = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(retval == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, message->binary_data, message->binary_data_length, "Binary data did not match");
    robusto_free(message->raw_data);
    robusto_free(message);
}
#endif

bool esp_now_tst_on_new_peer(robusto_peer_t *_peer)
{
    ROB_LOGI("TEST", "In on_new_peer");
    incoming_peer = _peer;
    async_receive_flag = true;
    return true;
}

void tst_esp_now_message_receive_presentation(void)
{
    ROB_LOGI("Test", "In tst_esp_now_message_receive_presentation");
    // Register the on work callback
    robusto_register_on_new_peer(&esp_now_tst_on_new_peer);
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
    // TODO: We need more tests here. I think.
}

void esp_now_tst_do_on_work(incoming_queue_item_t *_incoming_item)
{
    ROB_LOGI("TEST", "In esp_now_tst_do_on_work");
    incoming_message = _incoming_item->message;
    async_receive_flag = true;
    _incoming_item->recipient_frees_message = true;
}

void tst_esp_now_message_receive_string_message(void)
{

    // Register the on work callback
    robusto_register_handler(esp_now_tst_do_on_work);
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

void tst_esp_now_message_receive_fragmented_message(void)
{

    // Register the on work callback
    robusto_register_handler(esp_now_tst_do_on_work);
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