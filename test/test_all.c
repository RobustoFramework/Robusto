#include "tst_header.h"
#ifdef ARDUINO_ARCH_MBED
#include "arduino_workarounds.hpp"
#endif

#include <robusto_init.h>
#include <robusto_logging.h>
#include <robusto_network_init.h>
#include <robusto_server_init.h>
#include <robusto_concurrency.h>
/* Test */

#include "tst_time.h"
#include "tst_defs.h"

#include "tst_logging.h"
#include "tst_message_building.h"
#include "tst_system.h"
#include "tst_media.h"
#include "tst_input.h"

#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER) ||  defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include "tst_pubsub.h"
#endif


#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "tst_message_sending_mock.h"
#include "tst_message_receiving_mock.h"
#include "tst_message_presentation_mock.h"
#include "tst_message_service_mock.h"
#include "tst_message_heartbeat_mock.h"
#include "tst_fragmentation.h" // TODO: Better naming?
#include "tst_qos_scenarios_mock.h"
#endif

#include "tst_concurrency.h"
#include "tst_queue.h"

#ifdef CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "tst_i2c_message_sending.h"
#include "tst_i2c_message_receiving.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#include "tst_canbus_message_sending.h"
#include "tst_canbus_message_receiving.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include "tst_esp_now_message_sending.h"
#include "tst_esp_now_message_receiving.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include "tst_ble_message_sending.h"
#include "tst_ble_message_receiving.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "tst_lora_message_sending.h"
#include "tst_lora_message_receiving.h"
#endif

#endif /* CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING */

/**
 * @brief Quality of service testing enables code for several things that might cause problems for other kinds of testing.
 * TODO: Is this really that problematic? Except for tst_qos, could perhaps the sending part be in integration testing?
 * This might have to be reworked at some point; if QoS get centralized, the unit testing might not have to be as sectioned off.
 *
 */
#ifdef CONFIG_ROBUSTO_NETWORK_QOS_TESTING

#include "tst_qos.h"

/* Only run this on MCUs where we can do some sending */
#if defined(CONFIG_ROBUSTO_SUPPORTS_LORA) || defined(CONFIG_ROBUSTO_SUPPORTS_ESPNOW) || defined(CONFIG_ROBUSTO_SUPPORTS_I2C)
#include "tst_qos_sending.h"
#endif

#endif



void setUp(void)
{
}

void tearDown(void)
{
}

void runUnityTests(void *pvParameters)
{
    // Initialize Robusto here, setup is per test.
    // We need control over the test, and do not want the test
    // runner to find tests on its own, hence the tst_ prefix.
    // It is true that makes test not working by themselves, but that is a trade off

    // TODO: These initialisation should probably be moved to later, and out if this file as well.


#ifdef ARDUINO_ARCH_MBED
    init_arduino_mbed();
#endif
    char log_prefix[8] = "Robusto\x00";
    ROB_LOGW(log_prefix, "Robusto starting.");
    init_robusto();
    ROB_LOGW(log_prefix, "Robusto started.");
#ifdef USE_NATIVE
    ROB_LOGW("NATIVE", "Waiting a short while to let things start.");
    // TODO: Something is obviously not waiting properly for something
    r_delay(1000);
    ROB_LOGW("NATIVE", "Done waiting.");
#endif

#if defined(USE_ESPIDF)
    robusto_watchdog_set_timeout(200);
#endif

    UNITY_BEGIN();
#ifndef CONFIG_ROBUSTO_NETWORK_QOS_TESTING
    

    RUN_TEST(tst_millis);
    robusto_yield();

    RUN_TEST(tst_blink);
    robusto_yield();

    RUN_TEST(tst_logging);
    robusto_yield();
    RUN_TEST(tst_bit_logging);
    robusto_yield();
    /**
     * @brief Unit tests for creating and checking Robusto messages
     *
     */
    RUN_TEST(tst_make_strings_message);
    robusto_yield();
    RUN_TEST(tst_make_binary_message);
    robusto_yield();
    RUN_TEST(tst_make_multi_message);
    robusto_yield();

    RUN_TEST(tst_make_multi_conversation_message);
    robusto_yield();
    RUN_TEST(tst_build_strings_data);
    robusto_yield();

    RUN_TEST(tst_calc_message_crc); // TODO: This should actually be able to work on the Arduino, but i seems to go 16 bit somewhere.
    robusto_yield();

#ifdef CONFIG_ROBUSTO_INPUT
    RUN_TEST(tst_input_adc_single_resolve);
    robusto_yield();

    RUN_TEST(tst_input_adc_multiple_resolve);
    robusto_yield();
#endif

#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER) ||  defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
    RUN_TEST(tst_pubsub); 
    robusto_yield();
#endif
    // TODO: Add a message parsing unit test

    /**
     * @brief Test sending messaging using a mock backend
     *
     */

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

    // Adds the MOCK media type to the host, and tests if it was added
    RUN_TEST(tst_add_host_media_type_mock);
    robusto_yield();
    ROB_LOGW("TEST", "Adding test peer.");
    // Adds 
    /* TODO: This adds the TEST_MOCK peer, perhaps this should not be done in the test*/

    init_defs_mock();

    ROB_LOGW("TEST", "Done waiting.");

    /* Synchronous testing*/
    RUN_TEST(tst_sync_mock_send_message);
    robusto_yield();
    
    RUN_TEST(tst_sync_mock_receive_string_message);
    robusto_yield();

    RUN_TEST(tst_sync_mock_receive_binary_message);
    robusto_yield();

    RUN_TEST(tst_sync_mock_receive_multi_message);
    robusto_yield();


    RUN_TEST(tst_sync_mock_receive_binary_message_restricted);
    robusto_yield();

    /* Asynchronous testing*/
    RUN_TEST(tst_async_mock_send_message);
    robusto_yield();

    RUN_TEST(tst_async_mock_receive_string_message);
    robusto_yield();

    RUN_TEST(tst_async_mock_presentation);
    robusto_yield();
    // TODO: Add a do-not-run-long-running-tests setting
    /*
    RUN_TEST(tst_async_mock_heartbeats);
    robusto_yield();
*/

    RUN_TEST(tst_async_mock_service);
    robusto_yield();
    
    init_defs_mock();

    RUN_TEST(tst_fragmentation_complete);
    robusto_yield();
    RUN_TEST(tst_fragmentation_resending);
    robusto_yield();

    // TODO: We should make the mock QoS scenario work
    #if 0
    RUN_TEST(tst_qos_scenario_full_cycle_mock);
    robusto_yield();
    #endif

#endif

/**
 * @brief Test direct communication between actuall boards using test loops.
 * Here, each peer calls the next, and the last peer calling the first, closing the loop.
 * The next peer, and whether this peer begins and ends a loop, is defined in the config under Networking -> Chosen media -> Config -> Testing.
 */
#ifdef CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C

    /* Asynchronous testing*/
#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)

    RUN_TEST(tst_i2c_message_send_presentation, "TEST DEST");
    robusto_yield();

#endif

    RUN_TEST(tst_i2c_message_receive_presentation);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)
    RUN_TEST(tst_i2c_message_send_presentation, "TEST NEXT");
    robusto_yield();

#endif

#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)
    RUN_TEST(tst_i2c_message_send_message);
    robusto_yield();
#endif

    RUN_TEST(tst_i2c_message_receive_string_message);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)
    RUN_TEST(tst_i2c_message_send_message);
    robusto_yield();
#endif

#if 0
    /**
     * @brief Below is a test loop 
     */

    /* If the current device is the initator, it begins the loop by sending a message*/
#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)
    RUN_TEST(tst_i2c_message_send_message);
    robusto_yield();
#endif

    /* All other devices will start by listening. (start these devices first) */
    RUN_TEST(tst_i2c_message_receive_string_message);
    robusto_yield();

    /* If you are not the initiator, call the next.  */
#if !defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR) && CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR > -1
#if CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR == 1
        //TODO: This is when you just run the initiator and a new board and the receipt doesn't work. Ugly AF.
        r_delay(2000);
#endif
    r_delay(2000);
    RUN_TEST(tst_i2c_message_send_message);
    robusto_yield();
#endif
#endif

#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS

    /* Asynchronous testing*/
#if CONFIG_ROB_NETWORK_TEST_CANBUS_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_CANBUS_LOOP_INITIATOR)

    RUN_TEST(tst_canbus_message_send_presentation, "TEST DEST");
    robusto_yield();

#endif

    RUN_TEST(tst_canbus_message_receive_presentation);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_CANBUS_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_CANBUS_LOOP_INITIATOR)
    RUN_TEST(tst_canbus_message_send_presentation, "TEST NEXT");
    robusto_yield();

#endif

#if CONFIG_ROB_NETWORK_TEST_CANBUS_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_CANBUS_LOOP_INITIATOR)
    RUN_TEST(tst_canbus_message_send_message);
    robusto_yield();
#endif

    RUN_TEST(tst_canbus_message_receive_string_message);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_CANBUS_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_I2C_LOOP_INITIATOR)
    RUN_TEST(tst_canbus_message_send_message);
    robusto_yield();
#endif

#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW

    /* Asynchronous testing*/
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)

    RUN_TEST(tst_esp_now_message_send_presentation, "TEST DEST");
    robusto_yield();

#endif

    RUN_TEST(tst_esp_now_message_receive_presentation);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)
    RUN_TEST(tst_esp_now_message_send_presentation, "TEST NEXT");
    robusto_yield();

#endif

#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)

    RUN_TEST(tst_esp_now_message_send_message);
    robusto_yield();
#endif

    RUN_TEST(tst_esp_now_message_receive_string_message);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)
    RUN_TEST(tst_esp_now_message_send_message);
    robusto_yield();
#endif

#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)
    RUN_TEST(tst_esp_now_message_send_message_fragmented);
    robusto_yield();
#endif

    RUN_TEST(tst_esp_now_message_receive_fragmented_message);
    robusto_yield();


#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)
    RUN_TEST(tst_esp_now_message_send_message_fragmented);
    robusto_yield();
#endif



#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE

    /* Asynchronous testing*/
#if CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_BLE_LOOP_INITIATOR)
    /* We will want to wait for BLE connections to establish */
    r_delay(4000);
    RUN_TEST(tst_ble_message_send_presentation, "TEST DEST");
    robusto_yield();

#endif

    RUN_TEST(tst_ble_message_receive_presentation);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_BLE_LOOP_INITIATOR)
    RUN_TEST(tst_ble_message_send_presentation, "TEST NEXT");
    robusto_yield();

#endif

#if CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_BLE_LOOP_INITIATOR)

    RUN_TEST(tst_ble_message_send_message);
    robusto_yield();
#endif

    RUN_TEST(tst_ble_message_receive_string_message);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_BLE_LOOP_INITIATOR)
    RUN_TEST(tst_ble_message_send_message);
    robusto_yield();
#endif
#if 0
#if CONFIG_ROB_NETWORK_TEST_BLE_CALL_ADDR > -1 && defined(CONFIG_ROB_NETWORK_TEST_BLE_LOOP_INITIATOR)
    RUN_TEST(tst_ble_message_send_message_fragmented);
    robusto_yield();
#endif

    RUN_TEST(tst_ble_message_receive_fragmented_message);
    robusto_yield();

#endif
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_ESP_NOW_LOOP_INITIATOR)
    RUN_TEST(tst_ble_message_send_message_fragmented);
    robusto_yield();
#endif



#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

/* TODO: Decide: Should we even do any synchronous communication?
 All relevant MCU:s have the ability to use queues, if they can't, it is uncertain what sync mode adds
 now after we have developed the base tech for each media.
 */
#if 0
#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
    

    RUN_TEST(tst_lora_message_send_message_sync);
    robusto_yield();
#endif
    
    RUN_TEST(tst_lora_message_receive_string_message_sync);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
    RUN_TEST(tst_lora_message_send_message_sync);
    robusto_yield();
#endif
#endif
/* Asynchronous testing*/
#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    r_delay(2000);
#endif
    RUN_TEST(tst_lora_message_send_presentation, "TEST DEST");
    robusto_yield();

#endif

    RUN_TEST(tst_lora_message_receive_presentation);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
    r_delay(2000);
    RUN_TEST(tst_lora_message_send_presentation, "TEST NEXT");
    robusto_yield();

#endif

#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR != 0xFFFFFFFFFFFF - 1 && defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
    RUN_TEST(tst_lora_message_send_message);
    robusto_yield();
#endif

    RUN_TEST(tst_lora_message_receive_string_message);
    robusto_yield();

#if CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR > -1 && !defined(CONFIG_ROB_NETWORK_TEST_LORA_LOOP_INITIATOR)
    RUN_TEST(tst_lora_message_send_message);
    robusto_yield();
#endif

#endif

#endif

    /**
     * @brief Test concurrency features, like tasks, workers and queues
     *
     */
    #if 0
    RUN_TEST(tst_task);
    robusto_yield();
    RUN_TEST(tst_queue_start);
    robusto_yield();
    RUN_TEST(tst_queue_check_poll);
    robusto_yield();
    RUN_TEST(tst_queue_add_work);
    robusto_yield();
    RUN_TEST(tst_queue_check_work);
    robusto_yield();
    RUN_TEST(tst_queue_shutdown);
    robusto_yield();
    #endif

#endif
#ifdef CONFIG_ROBUSTO_NETWORK_QOS_TESTING

    r_delay(1000);
    RUN_TEST(tst_qos);

    robusto_yield();
#if defined(USE_ESPIDF) || defined(USE_ARDUINO) || defined(ARDUINO_ARCH_STM32)

#if defined(CONFIG_ROBUSTO_NETWORK_QOS_INITIATOR)
    RUN_TEST(tst_qos_message_send_presentation, "TEST_QOS_TARGET");
    robusto_yield();
    RUN_TEST(tst_qos_start_sending_data);
    robusto_yield();

#endif
    /*
    RUN_TEST(tst_qos_start_receiving_data);
    robusto_yield();
    */

#if !defined(CONFIG_ROBUSTO_NETWORK_QOS_INITIATOR)
    RUN_TEST(tst_qos_start_sending_data);
    robusto_yield();
#endif
#endif
#endif
    
    UNITY_END();

#if defined(USE_ARDUINO) || defined(USE_ESPIDF)
    vTaskDelete(NULL);
#endif
}

TEST_ENTRY_POINT(runUnityTests)
