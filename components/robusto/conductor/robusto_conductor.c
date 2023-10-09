/**
 * @file robusto_orchestration.c
 * @author Nicklas Borjesson (nicklasb@gmail.com)
 * @brief Orchestration refers to process-level operations, like scheduling and timing sleep and timeboxing wake time
 * @version 0.1
 * @date 2022-11-20
 * 
 * @copyright Copyright Nicklas Borjesson(c) 2022
 * 
 */

#include "robusto_conductor.h"
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <esp_timer.h>
#endif
#include <robusto_sleep.h>

#include <robusto_messaging.h>
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

// TODO: esp_timer_get_time returns an int64_t, apparently for making it easier to calculate time differences?
// If so, should we do the same to avoid casting problems?

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
        next_time = SDP_AWAKE_TIME_uS + SDP_SLEEP_TIME_uS;
    }
    else
    {
        next_time = get_last_sleep_time() + SDP_SLEEP_TIME_uS + SDP_AWAKE_TIME_uS + SDP_SLEEP_TIME_uS;
    }
    ESP_LOGI(conductor_log_prefix, "Next time we are available is at %"PRIu64".", next_time);
}

void sdp_orchestration_parse_next_message(work_queue_item_t *queue_item)
{
    queue_item->peer->next_availability = get_time_since_start() + atoll(queue_item->parts[1]);
    ESP_LOGI(conductor_log_prefix, "Peer %s is available at %"PRIu64".", queue_item->peer->name, queue_item->peer->next_availability);
}

/**
 * @brief Sends a "NEXT"-message to a peer informing on microseconds to next availability window
 * Please note the limited precision of the chrystals.
 * TODO: If having several clients, perhaps a slight delay would be good?
 *
 * @param peer The peer to send the message to
 * @return int
 */
int sdp_orchestration_send_next_message(work_queue_item_t *queue_item)
{

    int retval;

    uint8_t *next_msg = NULL;

    ESP_LOGI(conductor_log_prefix, "BEFORE NEXT get_time_since_start() = %"PRIu64, get_time_since_start());
    /* TODO: Handle the 64bit loop-around after 79 days ? */
    uint32_t delta_next = next_time - get_time_since_start();
    ESP_LOGI(conductor_log_prefix, "BEFORE NEXT delta_next = %"PRIu64, delta_next);

    /*  Cannot send uint32_t into va_args in add_to_message */
    char * c_delta_next;
    asprintf(&c_delta_next, "%"PRIu64, delta_next);

    int next_length = add_to_message(&next_msg, "NEXT|%s|%i", c_delta_next, SDP_AWAKE_TIME_uS);

    if (next_length > 0)
    {
        retval = sdp_reply(*queue_item, ORCHESTRATION, next_msg, next_length);
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
 * @brief Send a "When"-message that asks the peer to describe themselves
 *
 * @return int A handle to the created conversation
 */
int sdp_orchestration_send_when_message(sdp_peer *peer)
{
    char when_msg[5] = "WHEN\0";
    return start_conversation(peer, ORCHESTRATION, "Orchestration", &when_msg, 5);
}

void sleep_until_peer_available(sdp_peer *peer, uint32_t margin_us)
{
    if (peer->next_availability > 0)
    {
        uint32_t sleep_length = peer->next_availability - get_time_since_start() + margin_us;
        ESP_LOGI(conductor_log_prefix, "Going to sleep for %"PRIu64" microseconds.", sleep_length);
        goto_sleep_for_microseconds(sleep_length);
    }
}

/**
 * @brief Ask to wait with sleep for a specific amount of time from now 
 * @param ask Returns false if request is denied
 */
bool ask_for_time(uint32_t ask) {
    // How long will we wait?  = The time (since boot) we started waiting + how long we are waiting -  time since (boot)
    uint32_t wait_time_left = + wait_for_sleep_started + wait_time - esp_timer_get_time() ;
    
    // Only request for more time if it is more than being available and already requested
    if ((wait_time_left < ask) && (requested_time < ask - wait_time_left)) {
        /* Only allow for requests that fit into the awake timebox */
        if (esp_timer_get_time() + wait_time_left + ask < SDP_AWAKE_TIMEBOX_uS) {
            requested_time = ask - wait_time_left;
            ESP_LOGI(conductor_log_prefix, "Orchestrator granted an extra %"PRIu64" ms of awakeness.", ask/1000);
            return true;
        } else {
            ESP_LOGE(conductor_log_prefix, "Orchestrator denied %"PRIu64" ms of awakeness because it would violate the timebox.", ask/1000);
            return false;      
        }
    } 
    ESP_LOGD(conductor_log_prefix, "Orchestrator got an unnessary request for %"PRIu64" ms of awakeness.", ask/1000);
    return true;

}

void take_control()
{
    /* Wait for the awake period*/
    wait_time = SDP_AWAKE_TIME_uS;
    int64_t wait_ms;
    while (1) {
        
        wait_ms = wait_time / 1000;
        ESP_LOGI(conductor_log_prefix, "Orchestrator awaiting sleep for %"PRIu64" ms.", wait_ms);
        wait_for_sleep_started = esp_timer_get_time();
        vTaskDelay(wait_ms / portTICK_PERIOD_MS);
        if (requested_time > 0) {
            wait_time = requested_time;
            ESP_LOGI(conductor_log_prefix, "Orchestrator extending the awake phase.");
            requested_time = 0;
        } else {
            ESP_LOGI(conductor_log_prefix, "------------Orchestrator done waiting, going to sleep! -----------");
            break;
        }

    }
    // TODO: Add this on other side as well
    if (on_before_sleep_cb)
    {
        ESP_LOGI(conductor_log_prefix, "----------------- Calling before sleep callback ------------------");
        if (!on_before_sleep_cb())
        {
            ESP_LOGW(conductor_log_prefix, "----------Stopped from going to sleep by callback!! -----------------");
            return;
        }
    }
    goto_sleep_for_microseconds(SDP_SLEEP_TIME_uS - (esp_timer_get_time() - SDP_AWAKE_TIME_uS));
}
/**
 * @brief Check with the peer when its available next, and goes to sleep until then.
 * 
 * @param peer The peer that one wants to follow
 */
void give_control(sdp_peer *peer)
{

    if (peer != NULL)
    {
        peer->next_availability = 0;
        // Ask for orchestration
        ESP_LOGI(conductor_log_prefix, "Asking for orchestration..");
        int retries = 0;
        // Waiting a while for a response.
        while (retries < 10)
        {
            sdp_orchestration_send_when_message(peer);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            
            if (peer->next_availability > 0)
            {
                break;
            }

            retries++;
        }
        if (retries == 10)
        {
            ESP_LOGE(conductor_log_prefix, "Haven't gotten an availability time for peer \"%s\" ! Tried %i times. Going to sleep for %i microseconds..",
                     peer->name, availibility_retry_count++, SDP_ORCHESTRATION_RETRY_WAIT_uS);
            
            goto_sleep_for_microseconds(SDP_ORCHESTRATION_RETRY_WAIT_uS);
        }
        else
        {
            availibility_retry_count = 0;
            ESP_LOGI(conductor_log_prefix, "Waiting for sleep..");
            /* TODO: Add a sdp task concept instead and wait for a task count to reach zero (within ) */
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            sleep_until_peer_available(peer, SDP_AWAKE_MARGIN_uS);
        }
    }
}

void robusto_conductor_init(char *_log_prefix, before_sleep _on_before_sleep_cb)
{
    conductor_log_prefix = _log_prefix;
    on_before_sleep_cb = _on_before_sleep_cb;
    // Set the next available time.
    update_next_availability_window();
}

