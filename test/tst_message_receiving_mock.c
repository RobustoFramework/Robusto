#include "tst_message_receiving_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <unity.h>
#include "tst_defs.h"
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_incoming.h>
#include <robusto_concurrency.h>

static bool async_receive_flag = false;
static incoming_queue_item_t *incoming_item = NULL;

void tst_sync_mock_receive_string_message(void)
{
    set_message_expectation(MMI_STRINGS);

    robusto_message_t *message;
    rob_ret_val_t res = robusto_receive_message_media_type(robusto_mt_mock, &message);

    TEST_ASSERT_MESSAGE(res == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, message->string_count, "The string count is wrong.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), message->strings[1], "Second string did not match");

    robusto_free(message->strings);
    robusto_free(message->raw_data);
    robusto_free(message);

}


void tst_sync_mock_receive_binary_message(void)
{
    set_message_expectation(MMI_BINARY);
    robusto_message_t *message;
    rob_ret_val_t res = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(res == ROB_OK, "Receive message did not return ROB_OK.");
    ROB_LOGI("sdf", "message->binary_data_length %u", message->binary_data_length);
    //rob_log_bit_mesh(ROB_LOG_INFO, "sdf", message->binary_data, message->binary_data_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, message->binary_data, message->binary_data_length, "Binary data did not match");
    robusto_free(message->raw_data);
    robusto_free(message);
}

void tst_sync_mock_receive_multi_message(void)
{
    set_message_expectation(MMI_MULTI);
    robusto_message_t *message;
    rob_ret_val_t res = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(res == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, message->binary_data, message->binary_data_length, "Binary data did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), message->strings[1], "Second string did not match");
    robusto_free(message->strings);
    robusto_free(message->raw_data);
    robusto_free(message);
}

void tst_sync_mock_receive_binary_message_restricted(void)
{
    set_message_expectation(MMI_BINARY_RESTRICTED);
    robusto_message_t *message;
    rob_ret_val_t res = robusto_receive_message_media_type(robusto_mt_mock, &message);
    TEST_ASSERT_MESSAGE(res == ROB_OK, "Receive message did not return ROB_OK.");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, 
    message->binary_data, message->binary_data_length,    "Binary data did not match");
    
    robusto_free(message->raw_data);
    robusto_free(message);
}



void mock_tst_do_on_work(incoming_queue_item_t *_incoming_item) {
    incoming_item = _incoming_item;
    async_receive_flag = true;   

    // The mock tests calls asynchronously, and needs some time
    r_delay(50);


    /* TODO:
    Services: 
        * A HI message should present available services in a byte key/ char *name format
        * Services should have an index from 0-255, enabling them to be specified by a single byte
    Incoming queue:
        * Media can themselves call the handler
        * The handler must be threadsafe and fast or spawn subtasks or smth. Probably not initially.
        * An app should basically not have to register or know about services that may or not may be. 
        * The app is registered as service 0. 
            * Or we have a "CALL" message type that infers a call to a service rather than the app. 
            * This could be easier for the app, and perhaps clearer.
            * And when calling a service, that could be a call to a string that is then mapped.
            * Or we just know what services there are. 
            * But if it was dynamic, services could act on that and be smarter.
                * For example, services could be made aware or able to listen in when peers with relevant capabilities are registered.
                * They may be clients, servers or whatever else a service might want to know about.
                * A mapping service could want to mapp all services on all peers.
                * An orchestrator would like to
    */ 
}

void tst_async_mock_receive_string_message(void)
{
    
    // Register the on work callback
    robusto_register_handler(mock_tst_do_on_work);
    async_receive_flag = false;
    incoming_item = NULL;

    // This makes the mock backend put a message on the incoming queue
    set_message_expectation(MMI_STRINGS_ASYNC);
    if (robusto_waitfor_bool(&async_receive_flag, 1000)) {
        ROB_LOGI("Test", "Async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    if (incoming_item == NULL) {
        TEST_FAIL_MESSAGE("Test failed, incoming_item NULL.");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, incoming_item->message->string_count, "The string count is wrong.");
    //ROB_LOGI("sdfsd", "%s %s",incoming_item->message->strings[0], incoming_item->message->strings[1]);
    //rob_log_bit_mesh(ROB_LOG_INFO,"sdfds", incoming_item->message->raw_data, incoming_item->message->raw_data_length);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(&tst_strings, incoming_item->message->strings[0], "First string did not match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE((char *)&(tst_strings[4]), incoming_item->message->strings[1], "Second string did not match");
    // TODO: We DO have to be able to free messages..

}

#endif