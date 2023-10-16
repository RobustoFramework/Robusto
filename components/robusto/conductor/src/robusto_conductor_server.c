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

#include <robusto_network_service.h>
#include <string.h>
#include <inttypes.h>

static char *conductor_log_prefix;
static before_sleep *on_before_sleep_cb;

/* When we should wake up next */
static uint32_t next_time;
/* When we started waiting to go to sleep */
static uint32_t wait_for_sleep_started;
/* How long we need to wait for sleep */
static uint32_t wait_time = 0;
/* Latest requested extra time so far */
static uint32_t requested_time = 0;

/* Store the moment we last went to sleep in persistent storage */
RTC_DATA_ATTR uint32_t last_sleep_time;
/* How long we were awake last */
RTC_DATA_ATTR uint32_t last_wake_time;

static void on_incoming_conductor_server(robusto_message_t *message);
static void on_shutting_down_conductor_server(robusto_message_t *message);

static network_service_t conductor_server_service = {
    .service_name = "Conductor server service",
    .incoming_callback = &on_incoming_conductor_server,
    .service_id = ROBUSTO_CONDUCTOR_SERVER_SERVICE_ID,
    .shutdown_callback = &on_shutting_down_conductor_server,
};

void on_shutting_down_conductor_server(robusto_message_t *message)
{
    ROB_LOGI(conductor_log_prefix, "Conductor server is shutting down");
}

void on_incoming_conductor_server(robusto_message_t *message)
{

    if (message->binary_data[0] == ROBUSTO_CONDUCTOR_MSG_WHEN)
    {
        robusto_conductor_server_send_then_message(message->peer);
    }
    else
    {
        ROB_LOGI(conductor_log_prefix, "Peer %s asked server something we didn't understand:", message->peer->name);
        rob_log_bit_mesh(ROB_LOG_INFO, conductor_log_prefix, message->binary_data, message->binary_data_length);
    }
}

/**
 * @brief Get total time asleep, awake and milliseconds since starting conducting
 *
 * @return int The number of milliseconds 
 */
uint32_t robusto_get_time_since_start()
{
    if (last_sleep_time > 0)
    {
        /* The time we fell asleep + the time we slept + the time since waking up = Total time*/
        return last_sleep_time + get_last_sleep_duration() + r_millis();
    }
    else
    {
        /* Can't include the cycle delay if we haven't cycled.. */
        return r_millis();
    }
}

/**
 * @brief Calculate the next time the conductor will wake up
 */
void robusto_conductor_server_calc_next_time()
{
    /* Next time  = Time now + Cycle time - how long we've been awake */
    next_time = robusto_get_time_since_start() + CONFIG_ROBUSTO_CONDUCTOR_SERVER_CYCLE_TIME_MS - r_millis();
    ROB_LOGI(conductor_log_prefix, "Next time we are available is at %" PRIu32 ".", next_time);
}

/**
 * @brief Sends a message with time in milliseconds tp next availability window
 * Please note the limited precision of the chrystals.
 *
 * @param peer The peer to send the message to
 * @return int
 */
int robusto_conductor_server_send_then_message(robusto_peer_t *peer)
{
    int retval;

    // The peer is obviously a client that will go to sleep, stop checking on it.
    peer->sleeper = true;

    ROB_LOGI(conductor_log_prefix, "BEFORE NEXT get_time_since_start() = %" PRIu32, robusto_get_time_since_start());

    uint32_t delta_next = next_time - robusto_get_time_since_start();
    ROB_LOGI(conductor_log_prefix, "BEFORE NEXT delta_next = %" PRIu32, delta_next);

    /*  Cannot send uint32_t into va_args in add_to_message */
    uint8_t *c_delta_next = robusto_malloc(5);
    c_delta_next[0] = ROBUSTO_CONDUCTOR_MSG_THEN;
    memcpy(c_delta_next + 1, &delta_next, sizeof(uint32_t));

    // TODO: do we need to await response here? Won't the client re-ask?
    return retval = send_message_binary(peer, ROBUSTO_CONDUCTOR_CLIENT_SERVICE_ID, 0, c_delta_next, 5, NULL);
    robusto_free(c_delta_next);
}

bool robusto_conductor_server_ask_for_time(uint32_t ask)
{
    // How long will we wait?  = The time (since boot) we started waiting + how long we are waiting -  time since (boot)
    uint32_t wait_time_left = wait_for_sleep_started + wait_time - r_millis();

    // Only request for more time if it is more than being available and already requested
    if ((wait_time_left < ask) && (requested_time < ask - wait_time_left))
    {
        /* Only allow for requests that fit into the awake timebox */
        if (r_millis() + wait_time_left + ask < ROBUSTO_AWAKE_TIMEBOX_MS)
        {
            requested_time = ask - wait_time_left;
            ROB_LOGI(conductor_log_prefix, "Orchestrator granted an extra %" PRIu32 " ms of awakeness.", ask / 1000);
            return true;
        }
        else
        {
            ROB_LOGE(conductor_log_prefix, "Orchestrator denied %" PRIu32 " ms of awakeness because it would violate the timebox.", ask / 1000);
            return false;
        }
    }
    ROB_LOGD(conductor_log_prefix, "Orchestrator got an unnessary request for %" PRIu32 " ms of awakeness.", ask / 1000);
    return true;
}

void robusto_conductor_server_take_control()
{
    /* Wait for the awake period*/
    wait_time = CONFIG_ROBUSTO_CONDUCTOR_SERVER_AWAKE_TIME_MS - r_millis();
    int32_t wait_ms;
    while (1)
    {
        if (r_millis() + wait_time > ROBUSTO_AWAKE_TIMEBOX_MS)
        {
            ROB_LOGE(conductor_log_prefix, "Conductor has reached its timebox limit. Shouldn't happen if ask for time works.");
            break;
        }
        wait_ms = wait_time;
        ROB_LOGI(conductor_log_prefix, "Hi there! awaiting sleep for %" PRIu32 " ms.", wait_ms);
        wait_for_sleep_started = r_millis();
        r_delay(wait_ms);
        if (requested_time > 0)
        {
            wait_time = requested_time;
            ROB_LOGI(conductor_log_prefix, "Conductor extending the awake phase.");
            requested_time = 0;
        }
        else
        {
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
    /* Set the sleep time just before going to sleep. */
    last_sleep_time = robusto_get_time_since_start();
    /* remember how long we were awake */
    // Todo: We could probably remove last_wake_time
    last_wake_time = r_millis();
    robusto_goto_sleep(CONFIG_ROBUSTO_CONDUCTOR_SERVER_CYCLE_TIME_MS - last_wake_time);
}

void robusto_conductor_server_init(char *_log_prefix, before_sleep _on_before_sleep_cb)
{
    conductor_log_prefix = _log_prefix;
    on_before_sleep_cb = _on_before_sleep_cb;
    // No sleep time has happened if we don't return from sleep mode.
    if (robusto_is_first_boot())
    {
        last_sleep_time = 0;
    }

    // Set the next available time.
    robusto_conductor_server_calc_next_time();
    robusto_register_network_service(&conductor_server_service);
    ROB_LOGI(conductor_log_prefix, "Conductor server initiated.");

}

#endif
