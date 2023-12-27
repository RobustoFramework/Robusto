/**
 * @file robusto_conductor.c
 * @author Nicklas Borjesson (<nicklasb at gmail dot com>)
 * @brief Conducting refers to process-level operations, like scheduling and timing sleep and timeboxing wake time
 * @version 0.1
 * @date 2022-11-20
 *
 * @copyright Copyright Nicklas Borjesson(c) 2022
 *
 */

#include <robusto_conductor.h>
#include <robusto_init_internal.h>

#ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT

#include <robusto_sleep.h>
#include <robusto_init.h>
#include <robusto_message.h>
#include <robusto_network_service.h>
#include <inttypes.h>

static char *conductor_log_prefix;

ROB_RTC_DATA_ATTR int availability_retry_count;

robusto_peer_t * main_conductor_peer = NULL;

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
        message->peer->next_availability = r_millis() + time_until_available;
        ROB_LOGI(conductor_log_prefix, "Peer %s sent us %" PRIu32 " and is available at %" PRIu32 ".", message->peer->name, time_until_available, message->peer->next_availability);
    } else {
        ROB_LOGE(conductor_log_prefix, "Conductor %s server sent something we didn't understand:", message->peer->name);
        rob_log_bit_mesh(ROB_LOG_ERROR, conductor_log_prefix, message->binary_data, message->binary_data_length);
    }
    
}


robusto_peer_t * robusto_conductor_client_get_conductor() {
    if (!main_conductor_peer) {
        ROB_LOGE(conductor_log_prefix, "The main conductor it not set.");
        return NULL;
    } else
    if (main_conductor_peer->state < PEER_KNOWN_INSECURE) {
        if (peer_waitfor_at_least_state(main_conductor_peer, PEER_KNOWN_INSECURE, 6000)) {
            return main_conductor_peer;
        } else {
            ROB_LOGE(conductor_log_prefix, "The main conductor has not reached at least PEER_KNOWN_INSECURE status.");
            return NULL;
        }
    } else {
        return main_conductor_peer;
    }
    
}

int robusto_conductor_client_send_when_message()
{
    uint8_t when_msg = ROBUSTO_CONDUCTOR_MSG_WHEN;
    return send_message_binary(robusto_conductor_client_get_conductor(), ROBUSTO_CONDUCTOR_SERVER_SERVICE_ID, 0, &when_msg, 1, NULL);
}

/**
 * @brief Sleep until the next available window
 *
 * @param peer The conductor peer
 * @return int
 */
void robusto_conductor_client_sleep_until_available(uint32_t margin_us)
{
    if (robusto_conductor_client_get_conductor()->next_availability > 0)
    {
        uint32_t sleep_length = robusto_conductor_client_get_conductor()->next_availability - r_millis() + margin_us;
        ROB_LOGI(conductor_log_prefix, "Going to sleep for %" PRIu32 " milliseconds.", sleep_length);
        robusto_goto_sleep(sleep_length);
    }
}

void robusto_conductor_client_give_control()
{

    if (robusto_conductor_client_get_conductor() != NULL)
    {
        robusto_conductor_client_get_conductor()->next_availability = 0;
        // Ask for conducting
        ROB_LOGI(conductor_log_prefix, "Asking for conducting..");
        int retries = 0;
        // Waiting a while for a response.
        while (retries < 10)
        {
            robusto_conductor_client_send_when_message();
            r_delay(5000);

            if (robusto_conductor_client_get_conductor()->next_availability > 0)
            {
                break;
            }

            retries++;
        }
        if (retries == 10)
        {
            ROB_LOGE(conductor_log_prefix, "Haven't gotten an availability time for peer \"%s\" ! Tried %i times. Going to sleep for %i seconds..",
                     robusto_conductor_client_get_conductor()->name, availability_retry_count++, CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_S);

            robusto_goto_sleep(CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_S * 1000);
        }
        else
        {
            availability_retry_count = 0;
            ROB_LOGI(conductor_log_prefix, "Waiting for sleep..");
            /* TODO: Add a robusto task concept instead and wait for a task count to reach zero (within ) */
            // r_delay(5000);
            robusto_conductor_client_sleep_until_available(CONFIG_ROBUSTO_CONDUCTOR_AWAKE_MARGIN_MS);
        }
    }
}

void robusto_add_conductor() {

    #if defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_BLE)
        e_media_type media_type = robusto_mt_ble;
    #elif defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_ESP_NOW)
        e_media_type media_type = robusto_mt_espnow;
    #elif defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_LORA)
        e_media_type media_type = robusto_mt_lora;
    #elif defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_I2C)
        e_media_type media_type = robusto_mt_i2c;
    #else
        #error "No media type selected for initial contact with the conductor."
    #endif

    #if defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_I2C) && !defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_I2C_ADDRESS) 
        #error "The conductor I2C address must be defined when an I2C media is selected."
    #elif !defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MAC_ADDRESS)  
        #error "The conductor MAC address must be defined when non-I2C-media is selected."
    #endif
    // If we aren't in the first reset, the peer should already be there
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MEDIA_I2C
        main_conductor_peer = robusto_peers_find_peer_by_i2c_address(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_I2C_ADDRESS)
        if (!main_conductor_peer) {
            main_conductor_peer = add_peer_by_i2c_address("CONDUCTOR_MAIN", CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_I2C_ADDRESS)
        }
    #else
        main_conductor_peer = robusto_peers_find_peer_by_base_mac_address(kconfig_mac_to_6_bytes(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MAC_ADDRESS));
        if (!main_conductor_peer) {
            main_conductor_peer = add_peer_by_mac_address("CONDUCTOR_MAIN", kconfig_mac_to_6_bytes(CONFIG_ROBUSTO_CONDUCTOR_CLIENT_CONDUCTOR_MAC_ADDRESS), media_type);
        }
    #endif
}

void robusto_conductor_client_start()
{
    robusto_add_conductor();
}

void robusto_conductor_client_stop()
{
}


void robusto_conductor_client_init(char *_log_prefix)
{
    conductor_log_prefix = _log_prefix;
    robusto_register_network_service(&conductor_client_service);
}

void robusto_conductor_client_register_service()
{
    register_service(robusto_conductor_client_init, robusto_conductor_client_start, robusto_conductor_client_stop, 4, "Conductor client service");    
}


#endif
