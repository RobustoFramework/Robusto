#include "tst_message_heartbeat_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <unity.h>
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_concurrency.h>

void tst_async_mock_heartbeats(void)
{

    robusto_peer_t *peer = robusto_peers_find_peer_by_name("TEST_MOCK");
    TEST_ASSERT_MESSAGE(peer != NULL, "The TEST MOCK peer is not set");

    uint64_t start_time = r_millis();
    set_message_expectation(MMI_HEARTBEAT);
       
    uint64_t delay = CONFIG_ROBUSTO_REPEATER_DELAY_MS * CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT;
   
    r_delay(delay *3);
    
    //ROB_LOGI("", "AFTR %llu %llu", peer->mock_info.last_send , start_time);

    TEST_ASSERT_MESSAGE(peer->mock_info.last_send > start_time, "No heartbeat has been sent during idle");
    // This is done with the host peer instead. Basically not much is tested, to be honest.
    TEST_ASSERT_MESSAGE(peer->mock_info.last_receive > start_time-100, "No sign of life has been received during idle");

}

#endif