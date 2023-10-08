#include "tst_header.h"
#ifdef ARDUINO_ARCH_MBED
//#include "arduino_workarounds.hpp"
#endif



//#include "unity_config.h"
#if 0
#include <robusto_init.h>
#include <robusto_network_init.h>
#include <robusto_logging.h>
#include <robusto_concurrency.h>

/* Test */

#include "tst_time.h"
#include "tst_defs.h"

#include "tst_logging.h"
#include "tst_message_building.h"
#include "tst_system.h"

#include "tst_media.h"



#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "tst_message_sending_mock.h"
#include "tst_message_receiving_mock.h"
#endif

#ifndef CONFIG_ROB_SYNCHRONOUS_MODE
#include "tst_concurrency.h"
#include "tst_queue.h"
#include <robusto_concurrency.h>
#endif 

#ifdef CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "tst_i2c_message_sending.h"
#include "tst_i2c_message_receiving.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include "tst_esp_now_message_sending.h"
#include "tst_esp_now_message_receiving.h"
#endif

#endif /* CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING */
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
    //init_arduino_mbed();
#endif
#if 0

    init_robusto();
    robusto_network_init("Robusto");

#if defined(ESP_PLATFORM)
    robusto_watchdog_set_timeout(200);

#endif


    UNITY_BEGIN(); 
#if 0
    RUN_TEST(tst_millis);
    robusto_yield();

    RUN_TEST(tst_blink);
    robusto_yield();

    RUN_TEST(tst_logging);
    robusto_yield();
    RUN_TEST(tst_bit_logging);
    robusto_yield();
   
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

#ifndef CONFIG_ROB_SYNCHRONOUS_MODE
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

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C

    RUN_TEST(tst_add_host_media_type_i2c);
    robusto_yield();
#endif


#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
    RUN_TEST(tst_message_send_message_media_type_mock);
    robusto_yield();

    RUN_TEST(tst_message_receive_string_message_mock);
    robusto_yield();

    RUN_TEST(tst_message_receive_binary_message_mock);
    robusto_yield();
    RUN_TEST(tst_message_receive_multi_message_mock);
    robusto_yield();

    RUN_TEST(tst_message_receive_binary_message_restricted_mock);
    robusto_yield();

#endif

#ifdef CONFIG_ROBUSTO_NETWORK_INTEGRATION_TESTING

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
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
    
    RUN_TEST(tst_i2c_message_send_message);
    robusto_yield();
    #endif

    
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
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
#endif
#endif

#endif
    UNITY_END();

#if defined(ARDUINO) || defined(ESP_PLATFORM)
    vTaskDelete(NULL);
#endif
}

TEST_ENTRY_POINT(runUnityTests)


