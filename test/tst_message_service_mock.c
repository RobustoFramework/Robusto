#include "tst_message_service_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <unity.h>
#include <robusto_logging.h>
#include <robusto_incoming.h> 
#include <robusto_network_service.h>

#include "tst_defs.h"

void on_incoming(robusto_message_t *message);
void on_shutdown(void);

static bool async_receive_flag = false;
static robusto_message_t *reply_msg = NULL;

network_service_t mock_service = {
    service_id : 1959,
    service_name : "Mock service",
    service_frees_message : true, // We will free the message ourselves so we can examine it in the test
    incoming_callback : &on_incoming,
    shutdown_callback: &on_shutdown
};


void on_incoming(robusto_message_t *message) {
    reply_msg = message;
    async_receive_flag = true;

}

void on_shutdown(void) {

}

static uint16_t reply_msg_length = 0;


void tst_async_mock_service(void)
{
    TEST_ASSERT_MESSAGE(robusto_register_network_service(&mock_service) == ROB_OK, "Failed adding service");

    async_receive_flag = false;
    // This makes the mock backend start put a HI-message on the incoming queue
    set_message_expectation(MMI_SERVICE);
    if (robusto_waitfor_bool(&async_receive_flag, 100)) {
        ROB_LOGI("Test", "tst_async_mock_service async receive flag was set to true.");
    } else {
        TEST_FAIL_MESSAGE("Test failed, timed out.");
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&tst_binary, reply_msg->binary_data, reply_msg->binary_data_length, "Binary data did not match");
    robusto_message_free(reply_msg);
}

#endif