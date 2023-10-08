/**
 * @file tst_qos_scenarios_mock.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief These tests provokes the QoS state engine to cycle the media states
 * @version 0.1
 * @date 2023-08-04
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "tst_qos_scenarios_mock.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include <unity.h>
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_concurrency.h>
#include <robusto_qos.h>

/**
 * @brief This test provokes a mock media into cycling all states
 */
void tst_qos_scenario_full_cycle_mock(void)
{
    
    // First, find the mocked peer
    robusto_peer_t *peer = robusto_peers_find_peer_by_name("TEST_MOCK");
    uint64_t start_time = r_millis();

    TEST_ASSERT_MESSAGE(peer->mock_info.state == media_state_working, "The TEST_MOCK peer isn't in a working state, it must be so in this case.");
    // Send a normal heartbeat to it
    //set_message_expectation(MMI_HEARTBEAT);
    uint64_t delay = CONFIG_ROBUSTO_REPEATER_DELAY_MS * CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT;
    r_delay(delay + 50);

    TEST_ASSERT_MESSAGE(peer->mock_info.last_send > start_time, "No heartbeat has been sent during idle");

    // TODO: Find out how the tests below should work. 
    // Currently, this is basically too fast on Native, for example. There is no time for a peer to go through the motions.
    #if 0 

    // Wait for x until state is media_state_problem
    TEST_ASSERT_MESSAGE(robusto_waitfor_byte(&peer->mock_info.state, media_state_problem, delay * 2) != ROB_OK, "The TEST_MOCK didn't enter the problem state.");
    
    // Wait for x until state is media_state_recovery
    TEST_ASSERT_MESSAGE(robusto_waitfor_byte(&peer->mock_info.state, media_state_recovering, delay * 2) != ROB_OK, "The TEST_MOCK didn't enter the recovering state.");

    // Wait for x until state is media_state_down
    TEST_ASSERT_MESSAGE(robusto_waitfor_byte(&peer->mock_info.state, media_state_down, delay * 2) != ROB_OK, "The TEST_MOCK didn't enter the down state.");

    // Send a functional heartbeat to it
    set_message_expectation(MMI_HEARTBEAT);


    // Wait for x until state is media_state_working
    TEST_ASSERT_MESSAGE(robusto_waitfor_byte(&peer->mock_info.state, media_state_working, delay * 2) != ROB_OK, "The TEST_MOCK didn't finish in the media_state_working state.");
    #endif
}

// TODO: Test going to media_state_problem when we are getting heartbeats saying the peer isn't getting ours

#endif