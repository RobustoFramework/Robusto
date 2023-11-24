
/**
 * @file espnow_messaging.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Implementation of ESP-NOW media
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "espnow_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW

#include "espnow_messaging.h"
#include "espnow_queue.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_peer.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include "esp_crc.h"
#include "esp_now.h"

char *espnow_messaging_log_prefix;

#define ESPNOW_MAXDELAY 512
#define ESPNOW_FRAGMENT_SIZE (ESP_NOW_MAX_DATA_LEN - 10)

/* Used to identify multipart message data (as opposed to the initiating message) */
#define MULTIPART_DATA_CONTEXT MSG_MULTIPART + 0x40

static void espnow_deinit(espnow_send_param_t *send_param);

static bool has_receipt = false;
static esp_now_send_status_t send_status = ESP_NOW_SEND_FAIL;

// For synchronously reading async data
static uint8_t *synchro_data = NULL;
static int synchro_data_len = 0;
static robusto_peer_t *synchro_peer = NULL;

// TODO: If this works, centralize multipart handling for all medias (will a stream be similar?) This is the specific fragmented case
typedef struct fragmented_message
{

    uint32_t hash;
    uint32_t data_length;
    uint32_t fragment_count;
    uint32_t fragment_size;
    uint32_t curr_counter;
    uint8_t *data;
    uint8_t *received_fragments; // TODO: This should instead be a bitmap to save space.
    /* When the frag_msg was created, a non-used element */
    uint32_t start_time;
    SLIST_ENTRY(fragmented_message)
    fragmented_messages; /* Singly linked list */

} fragmented_message_t;

SLIST_HEAD(fragmented_message_head_t, fragmented_message);

struct fragmented_message_head_t fragmented_message_head;

// Cache the last frag_msg for optimization
fragmented_message_t *last_frag_msg;

/**
 * @brief Handle the receipt from the peer
 *
 * @param mac_addr The macaddress of the peer
 * @param status the
 */
static void espnow_receipt_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("ESP-NOW", "ESP-NOW KILL SWITCH ON - Failing receiving receipt");
        r_delay(100);
        return;
    }
#endif
    send_status = status;
    has_receipt = true;
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        ROB_LOGD(espnow_messaging_log_prefix, ">> In espnow_receipt_cb, send success.");
    }

    if (status == ESP_NOW_SEND_FAIL)
    {
        ROB_LOGW(espnow_messaging_log_prefix, ">> In espnow_receipt_cb, send failure, mac address:");
        rob_log_bit_mesh(ROB_LOG_WARN, espnow_messaging_log_prefix, mac_addr, ROBUSTO_MAC_ADDR_LEN);
        robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(mac_addr);
        if (peer)
        {
            peer->espnow_info.send_failures++;
        }
        else
        {
            ROB_LOGE(espnow_messaging_log_prefix, "espnow_receipt_cb() - no peer found matching dest_mac_address.");
        }
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len)
{
    if (strncmp((char *)(esp_now_info->des_addr), "\xff\xff\xff\xff\xff\xff", 6) == 0)
    {
        ROB_LOGW("ESP-NOW", "Somebody is sending to the broadcast address (FF:FF:FF:FF:FF:FF) on ESP-NOW, disregarding.");
        // TODO: We may actually want to make use of this mode, explore how it works.
        return;
    }
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("ESP-NOW", "ESP-NOW KILL SWITCH ON - Failing receiving");
        r_delay(100);
        return;
    }
#endif

    if (esp_now_info->src_addr == NULL || data == NULL || len <= 0)
    {
        ROB_LOGE(espnow_messaging_log_prefix, "<< In espnow_recv_cb, either parameter was NULL or 0.");
        return;
    }

    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(esp_now_info->src_addr);
    if (peer != NULL)
    {
        ROB_LOGI(espnow_messaging_log_prefix, "<< espnow_recv_cb got a message from a peer. rssi: %i, rate %u, data:",
                 esp_now_info->rx_ctrl->rssi, esp_now_info->rx_ctrl->rate);
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_messaging_log_prefix, data, len);
    }
    else
    {
        /* We didn't find the peer */
        ROB_LOGW(espnow_messaging_log_prefix, "<< espnow_recv_cb got a message from an unknown peer. Data:");
        rob_log_bit_mesh(ROB_LOG_WARN, espnow_messaging_log_prefix, data, len);
        ROB_LOGW(espnow_messaging_log_prefix, "<< Mac address:");
        rob_log_bit_mesh(ROB_LOG_WARN, espnow_messaging_log_prefix, esp_now_info->src_addr, ROBUSTO_MAC_ADDR_LEN);
        if (data[ROBUSTO_CRC_LENGTH] != 0x42)
        {
            // If it is unknown, and not a presentaition, disregard
            // TODO: Marking as a presentation sort of bypasses this, is this proper?
            return;
        }

        /* Remember peer. */

        esp_now_peer_info_t *espnow_peer = malloc(sizeof(esp_now_peer_info_t));

        espnow_peer->channel = 0;     // Use the current channel
        espnow_peer->encrypt = false; // TODO: No encryption currently, should we only use our own?
        espnow_peer->ifidx = ESP_IF_WIFI_STA;
        memcpy(espnow_peer->peer_addr, esp_now_info->src_addr, ROBUSTO_MAC_ADDR_LEN);

        peer = robusto_add_init_new_peer(NULL, esp_now_info->src_addr, robusto_mt_espnow);
    }

    bool is_heartbeat = data[ROBUSTO_CRC_LENGTH] == HEARTBEAT_CONTEXT;
    if (is_heartbeat)
    {
        ROB_LOGD(espnow_messaging_log_prefix, "ESP-NOW is heartbeat");
        rob_log_bit_mesh(ROB_LOG_DEBUG, espnow_messaging_log_prefix, data, len);
        peer->espnow_info.last_peer_receive = parse_heartbeat(data, ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
    }
    // TODO: Break out multipart handling, is not mediaspecific, obviously
    if (data[ROBUSTO_CRC_LENGTH] == MSG_MULTIPART)
    {
        // Initiate a new multipart  (...stream?)
        // Manually check CRC32 hash
        if (*(uint32_t *)(data) != robusto_crc32(0, data + 4, 17))
        {
            add_to_history(&peer->espnow_info, false, ROB_FAIL);
            ROB_LOGE(espnow_messaging_log_prefix, "Multipart request failed because hash mismatch.");
            return;
        }

        fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
        frag_msg->data_length = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 1);
        frag_msg->fragment_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 5);
        frag_msg->fragment_size = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 9);
        frag_msg->hash = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 13);
        // TODO: How big should we allow before SPIRAM and more?
        frag_msg->data = robusto_malloc(frag_msg->data_length);
        frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
        memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);

        ROB_LOGI(espnow_messaging_log_prefix, "Multipart initialized received, info:\n \
         data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
                 frag_msg->data_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);

        /* When the frag_msg was created, a non-used element */
        frag_msg->start_time = r_millis();
        SLIST_INSERT_HEAD(&fragmented_message_head, frag_msg, fragmented_messages);
        last_frag_msg = frag_msg;
        peer->espnow_info.last_receive = r_millis();
        return;
    }
    if (data[ROBUSTO_CRC_LENGTH] == MULTIPART_DATA_CONTEXT)
    {
        // Initiate a new multipart  (...stream?)
        fragmented_message_t *frag_msg;
        if (memcmp(last_frag_msg->hash, data, 4) != 0)
        {
            // Not the last multipart message, find it
            ROB_LOGE(espnow_messaging_log_prefix, "Time to implement looking up the fragment message");
            return;
        }
        else
        {
            frag_msg = last_frag_msg;
        }
        uint32_t msg_frag_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH);
        if (msg_frag_count == 0xFFFFFFFF) {
            
            // If received parts doesn't add up to fragment count, send list of missing parts to sender
            // TODO: if parts is more than 240 * 8 we can't send that much data. Implicitly that limits the message size to * 250 = 460800 bytes.
            // Note that we probably do not want to handle larger data typically, even though SPIRAM may support it. A larger ESP-NOW frame size would change that though.

            

            // Last, check hash and 


            if (frag_msg->hash != robusto_crc32(0, frag_msg->data, frag_msg->data_length)) {
                ROB_LOGE(espnow_messaging_log_prefix, "The full message did not match with the hash");
                // 
                return;

            } else {
                ROB_LOGI(espnow_messaging_log_prefix, "The full message matched the hash;");
                add_to_history(&peer->espnow_info, false, robusto_handle_incoming(frag_msg->data, frag_msg->data_length, peer, robusto_mt_espnow, 0));
                return;
            }
        }

        if (ROBUSTO_CRC_LENGTH + 4 + frag_msg->fragment_size == len)
        {
            // Length of the data checks out
            memcpy(frag_msg->data + (ESPNOW_FRAGMENT_SIZE * msg_frag_count), data + ROBUSTO_CRC_LENGTH + 4, frag_msg->fragment_size);
            frag_msg->received_fragments[msg_frag_count] = 1;
            
        }
        else
        {
            ROB_LOGE(espnow_messaging_log_prefix, "Time to implement looking up the multipart");
            return;
        }
    }

    peer->espnow_info.last_receive = r_millis();
    // Is there *really* a need for a synchronized version like below..probably only for testing the comms layer at some initial situation?
    synchro_data = data;
    synchro_data_len = len;
    synchro_peer = peer;
    if (is_heartbeat)
    {
        add_to_history(&peer->espnow_info, false, ROB_OK);
    }
    else
    {
        uint8_t *n_data = robusto_malloc(len);
        memcpy(n_data, data, len);
        add_to_history(&peer->espnow_info, false, robusto_handle_incoming(n_data, len, peer, robusto_mt_espnow, 0));
        // robusto_free(data);
    }
}

static esp_err_t espnow_init(void)
{

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_receipt_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
#if CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE
    ESP_ERROR_CHECK(esp_now_set_wake_window(65535));
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)(CONFIG_ESPNOW_PMK)));

    return ESP_OK;
}
/**
 * @brief Deinitialization of ESP-NOW -
 * TODO: Consider if this must be done before sleep modes
 *
 * @param send_param
 */
static void espnow_deinit(espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    esp_now_deinit();
}

rob_ret_val_t esp_now_send_check(rob_mac_address *base_mac_address, uint8_t *data, int data_length)
{

    int rc = esp_now_send(base_mac_address, data, data_length);
    if (rc != ESP_OK)
    {
        ROB_LOGE(espnow_messaging_log_prefix, "Mac address:");
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_messaging_log_prefix, base_mac_address, ROBUSTO_MAC_ADDR_LEN);
        if (rc == ESP_ERR_ESPNOW_NOT_INIT)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NOT_INIT");
        }
        else if (rc == ESP_ERR_ESPNOW_ARG)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_ARG");
        }
        else if (rc == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NOT_FOUND");
        }
        else if (rc == ESP_ERR_ESPNOW_NO_MEM)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NO_MEM");
        }
        else if (rc == ESP_ERR_ESPNOW_FULL)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_FULL");
        }
        else if (rc == ESP_ERR_ESPNOW_INTERNAL)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_INTERNAL");
        }
        else if (rc == ESP_ERR_ESPNOW_EXIST)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_EXIST");
        }
        else if (rc == ESP_ERR_ESPNOW_IF)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_IF");
        }
        else
        {
            ROB_LOGE(espnow_messaging_log_prefix, "ESP-NOW unknown error: %i", rc);
        }
        return -ROB_ERR_SEND_FAIL;
    }
    return ROB_OK;
}
/**
 * @brief Sends a message through ESPNOW.
 */
rob_ret_val_t esp_now_send_message_multipart(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("ESP-NOW", "ESP-NOW KILL SWITCH ON - Failing sending");
        r_delay(100);
        return ROB_FAIL;
    }
#endif
    int rc = ROB_FAIL;
    // How many parts will it have?
    uint32_t part_count = data_length / ESPNOW_FRAGMENT_SIZE;
    if (part_count % ESPNOW_FRAGMENT_SIZE > 0)
    {
        part_count++;
    }

    ROB_LOGI(espnow_messaging_log_prefix, "Creating and sending a %lu-part, %lu-byte multipart message in %i-byte chunks.", part_count, data_length, ESPNOW_FRAGMENT_SIZE);

    uint32_t curr_part = 0;
    uint8_t *buffer = robusto_malloc(ESPNOW_FRAGMENT_SIZE + sizeof(curr_part));

    // First, tell the peer that we are going to send it a multipart message
    // This is a message saying just that
    buffer[ROBUSTO_CRC_LENGTH] = MSG_MULTIPART;
    uint32_t fragment_size = ESPNOW_FRAGMENT_SIZE;
    uint32_t hash = robusto_crc32(0, data, data_length);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 1, &data_length, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 5, &part_count, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 9, &fragment_size, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 13, &hash, 4);
    uint32_t msg_hash = robusto_crc32(0, buffer + 4, 17);
    memcpy(buffer, &msg_hash, 4);
    ROB_LOGI(espnow_messaging_log_prefix, "ESP-NOW is heartbeat");
    rob_log_bit_mesh(ROB_LOG_INFO, espnow_messaging_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 17);
    if (esp_now_send_check(&(peer->base_mac_address), buffer, ROBUSTO_CRC_LENGTH + 17) != ROB_OK)
    {
        ROB_LOGE(espnow_messaging_log_prefix, "Could not initiate multipart messaging, got a failure sending");
        return ROB_FAIL;
    }

    // We want to wait to make shure the transmission succeeded.
    // There are integrity checks in ESP-NOW, so we do not need any CRC checks here.
    int64_t start = r_millis();
    has_receipt = false;
    // TODO: Should we have a separate timeout setting here? It is not like a healty ESP-NOW-peer would take more han milliseconds to send a receipt.
    while (!has_receipt && r_millis() < start + 2000)
    {
        robusto_yield();
    }

    if (has_receipt)
    {
        rc = send_status == ESP_NOW_SEND_SUCCESS ? ROB_OK : ROB_FAIL;
        if (rc == ROB_FAIL)
        {
            ROB_LOGE(espnow_messaging_log_prefix, "Could not initiate multipart messaging, got a negative receipt");
            return ROB_FAIL;
        }
        has_receipt = false;
    }
    else
    {
        ROB_LOGE(espnow_messaging_log_prefix, "Could not initiate multipart messaging, timeout.");
        return ROB_ERR_NO_RECEIPT;
    }
    has_receipt = false;
    // We always send the same hash, as an identifier
    memcpy(buffer, &hash, 4); 
    buffer[ROBUSTO_CRC_LENGTH] = MULTIPART_DATA_CONTEXT;
    // TODO: Do this against a map of parts to (re-) send. 
    while (curr_part < part_count)
    {
        // Counter
        memcpy(buffer + ROBUSTO_CRC_LENGTH + 1, &curr_part, sizeof(curr_part));
        // Data
        memcpy(buffer + ROBUSTO_CRC_LENGTH + 5 + sizeof(curr_part), data + (ESPNOW_FRAGMENT_SIZE * curr_part), ESPNOW_FRAGMENT_SIZE);

        if (esp_now_send_check(&(peer->base_mac_address), buffer, ESPNOW_FRAGMENT_SIZE + sizeof(curr_part)) != ROB_OK)
        {
            // TODO: We might want store failures to resend this
            
        }
        curr_part++;
    }
    robusto_free(buffer);

    // TODO: Send a message that says that we are done

    // TODO: Await response, resend parts if needeed. 

    // If still missing parts, .

    return ROB_OK;
}

/**
 * @brief Sends a message through ESPNOW.
 */
rob_ret_val_t esp_now_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("ESP-NOW", "ESP-NOW KILL SWITCH ON - Failing sending");
        r_delay(100);
        return ROB_FAIL;
    }
#endif

    if (data_length > (ESP_NOW_MAX_DATA_LEN - ROBUSTO_PREFIX_BYTES - 10))
    {
        ROB_LOGI(espnow_messaging_log_prefix, "Data length %lu is more than cutoff at %i bytes, sending multipart", data_length, ESP_NOW_MAX_DATA_LEN - ROBUSTO_PREFIX_BYTES - 10);
        return esp_now_send_message_multipart(peer, data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES, receipt);
    }

    has_receipt = false;
    int rc = esp_now_send_check(&(peer->base_mac_address), data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES);
    if (rc != ESP_OK)
    {
        return -ROB_ERR_SEND_FAIL;
    }
    // We want to wait to make shure the transmission succeeded.
    // There are integrity checks in ESP-NOW, so we do not need any CRC checks here.
    int64_t start = r_millis();
    // TODO: Should we have a separate timeout setting here? It is not like a healty ESP-NOW-peer would take more han milliseconds to send a receipt.
    while (!has_receipt && r_millis() < start + 2000)
    {
        robusto_yield();
    }

    if (has_receipt)
    {
        rc = send_status == ESP_NOW_SEND_SUCCESS ? ROB_OK : ROB_FAIL;
        has_receipt = false;
    }
    else
    {
        ROB_LOGE(espnow_messaging_log_prefix, "Sending failed, timeout.");
        rc = ROB_ERR_NO_RECEIPT;
    }

    return rc;
}
int esp_now_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes)
{
#if CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_ESP_NOW_KILL_SWITCH) == true)
    {
        ROB_LOGE("ESP-NOW", "ESP-NOW KILL SWITCH ON - Failing reading data");
        r_delay(100);
        return 0;
    }
#endif
    if (synchro_data_len > 0)
    {
        *rcv_data = synchro_data;
        *prefix_bytes = 0;
        *peer = synchro_peer;
        return synchro_data_len;
    }
    return 0;
};

void espnow_do_on_poll_cb(queue_context_t *q_context)
{

    robusto_peer_t *peer;
    uint8_t *rcv_data;
    uint8_t prefix_bytes;

    esp_now_read_data(&rcv_data, &peer, &prefix_bytes);
}

void espnow_do_on_work_cb(media_queue_item_t *work_item)
{

    ROB_LOGD(espnow_messaging_log_prefix, ">> In ESP-NOW work callback.");
    send_work_item(work_item, &(work_item->peer->espnow_info), robusto_mt_espnow, &esp_now_send_message, &espnow_do_on_poll_cb, espnow_get_queue_context());
}

void espnow_messaging_init(char *_log_prefix)
{
    espnow_messaging_log_prefix = _log_prefix;
    espnow_init();
    SLIST_INIT(&fragmented_message_head); /* Initialize the queue */
}

#endif