/**
 * @file robusto_conductor.c
 * @author Nicklas Borjesson (nicklasb@gmail.com)
 * @brief Conducting refers to process-level operations, like scheduling and timing sleep and timeboxing wake time
 * @version 0.1
 * @date 2022-11-20
 * 
 * @copyright Copyright Nicklas Borjesson(c) 2022
 * 
 */

#include "robusto_conductor.h"

#ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT

#include <robusto_sleep.h>

#include <robusto_message.h>
#include <inttypes.h>

static char *conductor_log_prefix;
static before_sleep *on_before_sleep_cb;

RTC_DATA_ATTR int availibility_retry_count;

void robusto_conductor_parse_next_message(robusto_message_t *message)
{
    message->peer->next_availability = get_time_since_start() + atoll(message->binary_data[1]);
    ROB_LOGI(conductor_log_prefix, "Peer %s is available at %"PRIu32".", queue_item->peer->name, queue_item->peer->next_availability);
}


/**
 * @brief Send a "When"-message that asks the peer to describe themselves
 *
 * @return int A handle to the created conversation
 */
int robusto_conductor_send_when_message(robusto_peer_t *peer)
{
    char when_msg[5] = "WHEN\0";
    return start_conversation(peer, ORCHESTRATION, "Orchestration", &when_msg, 5);
}

void sleep_until_peer_available(robusto_peer_t *peer, uint32_t margin_us)
{
    if (peer->next_availability > 0)
    {
        uint32_t sleep_length = peer->next_availability - get_time_since_start() + margin_us;
        ROB_LOGI(conductor_log_prefix, "Going to sleep for %"PRIu32" microseconds.", sleep_length);
        goto_sleep_for_microseconds(sleep_length);
    }
}

/**
 * @brief Check with the peer when its available next, and goes to sleep until then.
 * 
 * @param peer The peer that one wants to follow
 */
void give_control(robusto_peer_t *peer)
{

    if (peer != NULL)
    {
        peer->next_availability = 0;
        // Ask for conducting
        ROB_LOGI(conductor_log_prefix, "Asking for conducting..");
        int retries = 0;
        // Waiting a while for a response.
        while (retries < 10)
        {
            robusto_conductor_send_when_message(peer);
            r_delay(5000);
            
            if (peer->next_availability > 0)
            {
                break;
            }

            retries++;
        }
        if (retries == 10)
        {
            ROB_LOGE(conductor_log_prefix, "Haven't gotten an availability time for peer \"%s\" ! Tried %i times. Going to sleep for %i microseconds..",
                     peer->name, availibility_retry_count++, CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_MS);
            
            goto_sleep_for_microseconds(CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_MS);
        }
        else
        {
            availibility_retry_count = 0;
            ROB_LOGI(conductor_log_prefix, "Waiting for sleep..");
            /* TODO: Add a robusto task concept instead and wait for a task count to reach zero (within ) */
            r_delay(5000);
            sleep_until_peer_available(peer, CONFIG_ROBUSTO_CONDUCTOR_AWAKE_MARGIN_MS);
        }
    }
}

void robusto_conductor_client_init(char *_log_prefix, before_sleep _on_before_sleep_cb)
{
    conductor_log_prefix = _log_prefix;
    on_before_sleep_cb = _on_before_sleep_cb;
    // Set the next available time.
    update_next_availability_window();
}

#endif
