/**
 * @file robusto_sleep.c
 * @author Nicklas Borjesson (nicklasb@gmail.com)
 * @brief Manages and tracks how a peer goes to sleep and wakes up
 * @date 2022-10-22
 *
 * @copyright Copyright Nicklas Borjesson(c) 2022
 *
 */

#include "robusto_sleep.h"
// TODO: Ifdef depending om some setting?

#include "robusto_retval.h"
#ifdef ESP_PLATFORM
    #include "esp_sleep.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/timers.h"
#endif

#include "robusto_logging.h"
#include "robusto_time.h"



/* Store the moment we last went to sleep in persistent storage */
RTC_DATA_ATTR uint32_t last_sleep_time;
RTC_DATA_ATTR int sleep_count;
RTC_DATA_ATTR uint32_t wake_time;
RTC_DATA_ATTR uint32_t to_sleep;
bool b_first_boot;

char *sleep_log_prefix;

rob_ret_val_t sleep_milliseconds(uint32_t millisecs) {
#ifdef ESP_PLATFORM

    if (esp_sleep_enable_timer_wakeup(millisecs * 1000) == ESP_OK)
    {
        esp_deep_sleep_start();
    }
    else
    {
        ROB_LOGE(sleep_log_prefix, "Error going to sleep for %"PRIu32" milliseconds!", millisecs);
        return ROB_FAIL;
    }
#else 
    return ROB_FAIL;
#endif
}


void robusto_goto_sleep(uint32_t millisecs)
{

    ROB_LOGI(sleep_log_prefix, "---------------------------------------- S L E E P ----------------------------------------");
    ROB_LOGI(sleep_log_prefix, "At %li and going to sleep for %lu milliseconds", r_millis(), millisecs);


    sleep_count++;
    /* Now we know how long we were awake this time */
    wake_time+= r_millis();
    /* Set the sleep time just before going to sleep. */
    last_sleep_time = robusto_get_time_since_start();
    sleep_milliseconds(millisecs);

}
/**
 * @brief Tells if we woke from sleeping or not
 * 
 * @return true If we were sleeping
 * @return false If it was from first boot.
 */
bool robusto_is_first_boot() {
    return b_first_boot;
}

/**
 * @brief Initialization of sleep management
 *
 * @param _log_prefix
 * @return true Returns true if waking up from sleep
 * @return false Returns false if normal boot
 */
bool robusto_sleep_init(char *_log_prefix)
{
    // TODO: Do I need a wake stub like: https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/deep-sleep-stub.rst
    sleep_log_prefix = _log_prefix;

    #ifdef ESP_PLATFORM
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    switch (wakeup_cause)
    {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        ROB_LOGI(sleep_log_prefix, "-------------------------------------------------------------------------------------------");
        ROB_LOGI(sleep_log_prefix, "----------------------------------------- B O O T -----------------------------------------");
        ROB_LOGI(sleep_log_prefix, "-------------------------------------------------------------------------------------------");
        ROB_LOGI(sleep_log_prefix, "Normal boot, not returning from sleep mode.");
        // No sleep time has happened if we don't return from sleep mode.
        last_sleep_time = 0;
        sleep_count = 0;
        wake_time = 0;
        // TODO: Implement a callback before sleep ?
        //robusto_reset_rtc();
        b_first_boot = false;
        return false;
        break;

    default:
        ROB_LOGI(sleep_log_prefix, "----------------------------------------- W A K E -----------------------------------------");
        ROB_LOGI(sleep_log_prefix, "Returning from sleep mode.");
        b_first_boot = false;
        return true;
        break;
    }
    #endif
}

/**
 * @brief Get the number of sleeps
 * 
 * @return int 
 */
int robusto_get_sleep_count() {
    return sleep_count;
}

/**
 * @brief Get the last sleep time
 *
 * @return int
 */
uint32_t robusto_get_last_sleep_time()
{
    return last_sleep_time;
}
/**
 * @brief
 *
 * @return int
 */
uint32_t robusto_get_time_since_start()
{
    if (last_sleep_time > 0)
    {
        /* The time we fell asleep + the time we waited + the time since waking up = Total time*/
        return last_sleep_time + to_sleep + r_millis();
    }
    else
    {
        /* Can't include the cycle delay if we haven't cycled.. */
        return r_millis();
    }
}

uint32_t robusto_get_total_time_awake() {
    return wake_time + r_millis();
}
