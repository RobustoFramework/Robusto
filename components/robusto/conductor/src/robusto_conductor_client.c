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
#include <robusto_network_service.h>
#include <inttypes.h>

static char *conductor_log_prefix;

RTC_DATA_ATTR int availibility_retry_count;

static void on_incoming_conductor_client(robusto_message_t *message);
static void on_shutting_down_conductor_client(robusto_message_t *message);

static network_service_t conductor_client_service = {
    .service_name = "Conductor client service",
    .incoming_callback = &on_incoming_conductor_client,
    .service_id = ROBUSTO_CONDUCTOR_CLIENT_SERVICE_ID,
    .shutdown_callback = &on_shutting_down_conductor_client,
};

void on_shutting_down_conductor_client(robusto_message_t *message)
{
    ROB_LOGI(conductor_log_prefix, "Conductor client is shutting down");
}

void on_incoming_conductor_client(robusto_message_t *message)
{
    if (message->binary_data[0] == ROBUSTO_CONDUCTOR_MSG_THEN)
    {
        uint32_t time_until_available = *(uint32_t *)(message->binary_data + 1);
        message->peer->next_availability = robusto_get_time_since_start() + time_until_available;
        ROB_LOGI(conductor_log_prefix, "Peer %s sent us %" PRIu32 " and is available at %" PRIu32 ".", message->peer->name, time_until_available, message->peer->next_availability);
    } else {
        ROB_LOGE(conductor_log_prefix, "Condustor %s server sent something we didn't understand:", message->peer->name);
        rob_log_bit_mesh(ROB_LOG_ERROR, conductor_log_prefix, message->binary_data, message->binary_data_length);
    }
    
}

int robusto_conductor_client_send_when_message(robusto_peer_t *peer)
{
    uint8_t when_msg = ROBUSTO_CONDUCTOR_MSG_WHEN;
    return send_message_binary(peer, ROBUSTO_CONDUCTOR_SERVER_SERVICE_ID, 0, &when_msg, 1, NULL);
}

/**
 * @brief Sleep until the next available window
 *
 * @param peer The conductor peer
 * @return int
 */
void robusto_conductor_client_sleep_until_available(robusto_peer_t *peer, uint32_t margin_us)
{
    if (peer->next_availability > 0)
    {
        uint32_t sleep_length = peer->next_availability - robusto_get_time_since_start() + margin_us;
        ROB_LOGI(conductor_log_prefix, "Going to sleep for %" PRIu32 " microseconds.", sleep_length);
        robusto_goto_sleep(sleep_length);
    }
}

void robusto_conductor_client_give_control(robusto_peer_t *peer)
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
            robusto_conductor_client_send_when_message(peer);
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

            robusto_goto_sleep(CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_MS);
        }
        else
        {
            availibility_retry_count = 0;
            ROB_LOGI(conductor_log_prefix, "Waiting for sleep..");
            /* TODO: Add a robusto task concept instead and wait for a task count to reach zero (within ) */
            // r_delay(5000);
            robusto_conductor_client_sleep_until_available(peer, CONFIG_ROBUSTO_CONDUCTOR_AWAKE_MARGIN_MS);
        }
    }
}

void robusto_conductor_client_init(char *_log_prefix)
{
    conductor_log_prefix = _log_prefix;
    // Set the next available time.
    robusto_register_network_service(&conductor_client_service);
}

#endif
