/**
 * @file robusto_conductor_server.c
 * @author Nicklas Borjesson (nicklasb@gmail.com)
 * @brief Conducting refers to process-level operations, like scheduling and timing sleep and timeboxing wake time
 * @version 0.1
 * @date 2022-11-20
 * 
 * @copyright Copyright Nicklas Borjesson(c) 2022
 * 
 */

#include "robusto_conductor.h"

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER

#include <robusto_sleep.h>

#include <robusto_message.h>
#include <inttypes.h>

static char *conductor_log_prefix;
static before_sleep *on_before_sleep_cb;

static uint32_t next_time;
static uint32_t wait_for_sleep_started;
/* How long we need to wait for sleep */
static uint32_t wait_time = 0;
/* Latest requested time so far */
static uint32_t requested_time = 0;

RTC_DATA_ATTR int availibility_retry_count;

/**
 * @brief Save the current time into RTC memory
 *
 */
void update_next_availability_window()
{
    // Note that this value will roll over at certain points (49.7 days?)
    /* Next time  = Last time we fell asleep + how long we slept + how long we should've been up + how long we will sleep */
    if (get_last_sleep_time() == 0)
    {
        next_time = CONFIG_ROBUSTO_AWAKE_TIME_MS + CONFIG_ROBUSTO_SLEEP_TIME_MS;
    }
    else
    {
        next_time = get_last_sleep_time() + CONFIG_ROBUSTO_SLEEP_TIME_MS + CONFIG_ROBUSTO_AWAKE_TIME_MS + CONFIG_ROBUSTO_SLEEP_TIME_MS;
    }
    ROB_LOGI(conductor_log_prefix, "Next time we are available is at %"PRIu32".", next_time);
}

/**
 * @brief Sends a message with time in milliseconds tp next availability window
 * Please note the limited precision of the chrystals.
 *
 * @param peer The peer to send the message to
 * @return int
 */
int robusto_conductor_send_next_message(robusto_peer_t *peer)
{

    int retval;

    uint8_t *next_msg = NULL;

    ROB_LOGI(conductor_log_prefix, "BEFORE NEXT get_time_since_start() = %"PRIu32, get_time_since_start());
    
    uint32_t delta_next = next_time - get_time_since_start();
    ROB_LOGI(conductor_log_prefix, "BEFORE NEXT delta_next = %"PRIu32, delta_next);

    /*  Cannot send uint32_t into va_args in add_to_message */
    char * c_delta_next[5];
    c_delta_next[0] = ROBUSTO_CONDUCTOR_CLIENT_ID; 
    memcpy(c_delta_next, delta_next, sizeof(uint32_t));

    send_message_binary(peer)

    int next_length = add_to_message(&next_msg, "NEXT|%s|%i", c_delta_next, ROBUSTO_AWAKE_TIME_MS);

    if (next_length > 0)
    {
        retval = robusto_reply(*queue_item, ORCHESTRATION, next_msg, next_length);
    }
    else
    {
        // Returning the negative of the return value as that denotes an error.
        retval = -next_length;
    }
    free(next_msg);
    return retval;
}

/**
 * @brief Ask to wait with sleep for a specific amount of time from now 
 * @param ask Returns false if request is denied
 */
bool ask_for_time(uint32_t ask) {
    // How long will we wait?  = The time (since boot) we started waiting + how long we are waiting -  time since (boot)
    uint32_t wait_time_left = + wait_for_sleep_started + wait_time - r_millis() ;
    
    // Only request for more time if it is more than being available and already requested
    if ((wait_time_left < ask) && (requested_time < ask - wait_time_left)) {
        /* Only allow for requests that fit into the awake timebox */
        if (r_millis() + wait_time_left + ask < ROBUSTO_AWAKE_TIMEBOX_MS) {
            requested_time = ask - wait_time_left;
            ROB_LOGI(conductor_log_prefix, "Orchestrator granted an extra %"PRIu32" ms of awakeness.", ask/1000);
            return true;
        } else {
            ROB_LOGE(conductor_log_prefix, "Orchestrator denied %"PRIu32" ms of awakeness because it would violate the timebox.", ask/1000);
            return false;      
        }
    } 
    ROB_LOGD(conductor_log_prefix, "Orchestrator got an unnessary request for %"PRIu32" ms of awakeness.", ask/1000);
    return true;

}

void take_control()
{
    /* Wait for the awake period*/
    wait_time = CONFIG_ROBUSTO_AWAKE_TIME_MS;
    int32_t wait_ms;
    while (1) {
        
        wait_ms = wait_time;
        ROB_LOGI(conductor_log_prefix, "Orchestrator awaiting sleep for %"PRIu32" ms.", wait_ms);
        wait_for_sleep_started = r_millis();
        r_delay(wait_ms);
        if (requested_time > 0) {
            wait_time = requested_time;
            ROB_LOGI(conductor_log_prefix, "Orchestrator extending the awake phase.");
            requested_time = 0;
        } else {
            ROB_LOGI(conductor_log_prefix, "------------Orchestrator done waiting, going to sleep! -----------");
            break;
        }

    }
    // TODO: Add this on other side as well
    if (on_before_sleep_cb)
    {
        ROB_LOGI(conductor_log_prefix, "----------------- Calling before sleep callback ------------------");
        if (!on_before_sleep_cb())
        {
            ROB_LOGW(conductor_log_prefix, "----------Stopped from going to sleep by callback!! -----------------");
            return;
        }
    }
    goto_sleep_for_microseconds(CONFIG_ROBUSTO_SLEEP_TIME_MS - (r_millis() - CONFIG_ROBUSTO_AWAKE_TIME_MS));
}

void robusto_conductor_server_init(char *_log_prefix, before_sleep _on_before_sleep_cb)
{
    conductor_log_prefix = _log_prefix;
    on_before_sleep_cb = _on_before_sleep_cb;
    // Set the next available time.
    update_next_availability_window();
}

#endif
