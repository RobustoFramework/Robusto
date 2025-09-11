#include "tst_message_sending_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

#include <unity.h>
#ifdef USE_ARDUINO
#include <Arduino.h>
#endif
#include "tst_defs.h"

#include <robusto_media_def.h>
#include <string.h>

#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_repeater.h>
#ifdef USE_ESPIDF
#include <network/src/media/mock/mock_messaging.h>
#else
#include "../components/robusto/network/src/media/mock/mock_messaging.h"
#endif
/* We reserve the first byte that we use for internal signaling.
 * For example, when data has been transmitted, and how it went. */
#define SIGNAL_BYTE_LEN 1



void init_defs_mock()
{
    if (robusto_peers_find_peer_by_name("TEST_MOCK"))
    {
        return;
    }
    robusto_peer_t * test_peer_mock = robusto_add_init_new_peer("TEST_MOCK", (rob_mac_address *)kconfig_mac_to_6_bytes(11), robusto_mt_mock);
    ROB_LOGI("TEST", "ADDED TEST_MOCK");
    test_peer_mock->protocol_version = 0;
    test_peer_mock->relation_id_incoming = TST_RELATIONID_01;
    test_peer_mock->peer_handle = 0;
    // TODO: We run this twice, not sure why, probably to get the curr_info->send_successes up to properly calculate a score. 
    #ifndef ARDUINO
    run_all_repeaters_now();
    run_all_repeaters_now();
    #endif

}

#if 0

#ifdef USE_ARDUINO
#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif // __arm__

int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (USE_ARDUINO > 103 && USE_ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif // __arm__
}
#endif

#endif

void tst_async_mock_send_message(void)
{
    uint8_t *tst_strings_msg;

    //  rob_log_bit_mesh(ROB_LOG_INFO, "test_make_strings_message input", (uint8_t*)&tst_strings, sizeof(tst_strings));

    int tst_strings_length = robusto_make_multi_message_internal(MSG_MESSAGE, 1, 0, (uint8_t *)&tst_strings, 8, NULL, 0, &tst_strings_msg);
    set_message_expectation(MMI_STRINGS);
    queue_state *q_state = robusto_malloc(sizeof(queue_state));
    robusto_peer_t * test_peer_mock = robusto_peers_find_peer_by_name("TEST_MOCK");
    rob_ret_val_t retval = send_message_raw(test_peer_mock, robusto_mt_mock, tst_strings_msg, tst_strings_length, q_state, true);
    TEST_ASSERT_EQUAL_MESSAGE(ROB_OK, retval, "Not the right response, ie ROB_OK (0).");

    rob_ret_val_t ret_val;
    if (!robusto_waitfor_queue_state(q_state, 500, &ret_val))
    {
        ROB_LOGE("TEST", "Failed because %i", ret_val);
        TEST_FAIL_MESSAGE("Test failed, flag timed out or operation failed.");
    }
    else
    {
        ROB_LOGI("TEST", "Succeeded");
    }
    robusto_free_queue_state(q_state);
}

#endif
