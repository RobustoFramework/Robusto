
/**
 * @file espnow_messaging.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
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

#if 0
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#endif
#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include "esp_crc.h"
#include "esp_now.h"

static char *espnow_log_prefix;

#define ESPNOW_FRAGMENT_SIZE (ESP_NOW_MAX_DATA_LEN - 10)

static void espnow_deinit(espnow_send_param_t *send_param);

static bool has_receipt = false;
static int send_status = -1;


rob_ret_val_t esp_now_send_check(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{

    // TODO: Recommend more wifi TX buffers and add warning if not enabled
    
    ROB_LOGD(espnow_log_prefix, "esp_now_send_check, sending %lu bytes.", data_length);
    int rc = esp_now_send(&peer->base_mac_address, data, data_length);
    send_status = -1;
    has_receipt = false;

    if (rc != ESP_OK)
    {
        ROB_LOGE(espnow_log_prefix, "Mac address:");
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_log_prefix, &peer->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
        if (rc == ESP_ERR_ESPNOW_NOT_INIT)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NOT_INIT");
        }
        else if (rc == ESP_ERR_ESPNOW_ARG)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_ARG");
        }
        else if (rc == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NOT_FOUND");
        }
        else if (rc == ESP_ERR_ESPNOW_NO_MEM)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_NO_MEM - Will delay a short while to let it free its memory.");
            r_delay(3000);
        }
        else if (rc == ESP_ERR_ESPNOW_FULL)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_FULL");
        }
        else if (rc == ESP_ERR_ESPNOW_INTERNAL)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_INTERNAL");
        }
        else if (rc == ESP_ERR_ESPNOW_EXIST)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_EXIST");
        }
        else if (rc == ESP_ERR_ESPNOW_IF)
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW error: ESP_ERR_ESPNOW_IF");
        }
        else
        {
            ROB_LOGE(espnow_log_prefix, "ESP-NOW unknown error: %i", rc);
        }
        rc = -ROB_ERR_SEND_FAIL;
        add_to_history(&peer->espnow_info, true, rc);
        return rc;
    }
    else
    {
        rc = ROB_OK;
        add_to_history(&peer->espnow_info, true, rc);
    }
    // We will wait here until the message was sent, for 2 seconds
    int32_t start_send = r_millis();
    while ((send_status < 0) && (r_millis() < start_send + 2000))
    {
        robusto_yield();
    }

    if (send_status == ESP_NOW_SEND_FAIL) {
        ROB_LOGE(espnow_log_prefix, "ESP-NOW transmission failed completing after %lu ms. Peer: %s", r_millis() - start_send, peer->name);
        return ROB_FAIL;
    }
    if (send_status < 0) {
        ROB_LOGE(espnow_log_prefix, "ESP-NOW transmission didn't complete in time (2000 ms). Peer: %s", peer->name);
        return ROB_FAIL;
    }
    #ifndef ROBUSTO_ESP_NOW_USE_RECEIPT
    peer->espnow_info.last_peer_receive = peer->espnow_info.last_receive;
    return rc;
    #else
    if (!receipt)
    {
        return rc;
    }
    
    // We want to wait to make sure the transmission is received successfully.
    int32_t start = r_millis();
    while ((!has_receipt) && (r_millis() < start + 500))
    {
        robusto_yield();
    }
    if (has_receipt)
    {
        has_receipt = false;

        rc = (send_status == ESP_NOW_SEND_SUCCESS) ? ROB_OK : ROB_FAIL;
        add_to_history(&peer->espnow_info, false, rc);
        if (rc == ROB_FAIL)
        {

            ROB_LOGE(espnow_log_prefix, "ESP-NOW got a negative receipt. Peer: %s", peer->name);
            return ROB_FAIL;
        }
    }
    else
    {
        ROB_LOGE(espnow_log_prefix, "ESP-NOW timed out waiting for receipt, timeout. Peer: %s", peer->name);
        return ROB_ERR_NO_RECEIPT;
    }
    return rc;
    #endif
}

/**
 * @brief Is called when ESP-NOW stopped sending to a peer
 *
 * @param mac_addr The macaddress of the peer
 * @param status the
 */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
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

    if (status == ESP_NOW_SEND_SUCCESS)
    {
        ROB_LOGD(espnow_log_prefix, ">> In espnow_send_cb, send success.");
    }

    if (status == ESP_NOW_SEND_FAIL)
    {
        ROB_LOGW(espnow_log_prefix, ">> In espnow_send_cb, send failure, mac address:");
        rob_log_bit_mesh(ROB_LOG_WARN, espnow_log_prefix, mac_addr, ROBUSTO_MAC_ADDR_LEN);
        robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(mac_addr);
        if (peer)
        {
            // Yes, this is counted doubly, but this should not happen, we probably have some kind of issue
            add_to_history(&peer->espnow_info, true, ROB_FAIL);
        }
        else
        {
            ROB_LOGE(espnow_log_prefix, "espnow_send_cb() - no peer found matching dest_mac_address.");
        }
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len)
{
    bool handled = false;
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
        ROB_LOGE(espnow_log_prefix, "<< In espnow_recv_cb, either parameter was NULL or 0.");
        return;
    }

    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(esp_now_info->src_addr);
    if (peer != NULL)
    {
        ROB_LOGD(espnow_log_prefix, "<< espnow_recv_cb got a message from a peer. rssi: %i, rate %u, data:",
                 esp_now_info->rx_ctrl->rssi, esp_now_info->rx_ctrl->rate);
        rob_log_bit_mesh(ROB_LOG_DEBUG, espnow_log_prefix, data, len);
    }
    else
    {
        /* We didn't find the peer */
        ROB_LOGW(espnow_log_prefix, "<< espnow_recv_cb got a message from an unknown peer. Data:");
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_log_prefix, data, len);
        ROB_LOGI(espnow_log_prefix, "<< Mac address:");
        rob_log_bit_mesh(ROB_LOG_INFO, espnow_log_prefix, esp_now_info->src_addr, ROBUSTO_MAC_ADDR_LEN);
        if (data[ROBUSTO_CRC_LENGTH] != 0x42)
        {
            // If it is unknown, and not a presentation, disregard
            // TODO: Marking as a presentation sort of bypasses this, is this proper?
            return;
        }

        peer = robusto_add_init_new_peer(NULL, esp_now_info->src_addr, robusto_mt_espnow);
    }

    bool is_heartbeat = data[ROBUSTO_CRC_LENGTH] == HEARTBEAT_CONTEXT;
    if (is_heartbeat)
    {
        ROB_LOGD(espnow_log_prefix, "ESP-NOW is heartbeat");
        rob_log_bit_mesh(ROB_LOG_DEBUG, espnow_log_prefix, data, len);
        peer->espnow_info.last_peer_receive = parse_heartbeat(data, ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
    }
    if ((data[ROBUSTO_CRC_LENGTH] & 0b00000111) == MSG_FRAGMENTED)
    {
        uint8_t *n_data = robusto_malloc(len);
        memcpy(n_data, data, len);
        if (!handle_fragmented(peer, robusto_mt_espnow, n_data, len, ESPNOW_FRAGMENT_SIZE, &esp_now_send_check))
        {
            return;
        }
        handled = true;
    }

    peer->espnow_info.last_receive = r_millis();

    if (is_heartbeat)
    {
        add_to_history(&peer->espnow_info, false, ROB_OK);
    }
    else
    {
        #ifdef ROBUSTO_ESP_NOW_USE_RECEIPT        
        // It is a receipt, just update stats and return.
        if (len == 2 && data[0] == 0xff && data[1] == 0x00)
        {
            ROB_LOGI(espnow_log_prefix, "<< espnow_recv_cb got a receipt from %s.", peer->name);
            peer->espnow_info.last_peer_receive = peer->espnow_info.last_receive;
            add_to_history(&peer->espnow_info, false, ROB_OK);
            has_receipt = true;
            return;
        }
        if ((data[ROBUSTO_CRC_LENGTH] & 0b00000111) != MSG_NETWORK) {
            // Unless it is a network message, send a receipt
            uint8_t response[2];
            response[0] = 0xff;
            response[1] = 0x00;
            // NOTE: We are calling esp_now_send directly here as we are calling it inside of the receive callback
            if (esp_now_send(&peer->base_mac_address, &response, 2) != ROB_OK) {
                ROB_LOGE(espnow_log_prefix, ">> espnow_recv_cb failed to send a receipt to %s.", peer->name);
            }
        }
        #endif
        if (!handled)
        {
            // Copy data a ESP-NOW frees it
            uint8_t *n_data = robusto_malloc(len);
            memcpy(n_data, data, len);
            add_to_history(&peer->espnow_info, false, robusto_handle_incoming(n_data, len, peer, robusto_mt_espnow, 0));
        }
    }
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
        ROB_LOGI(espnow_log_prefix, "Data length %lu is more than cutoff at %i bytes, sending fragmented", data_length, ESP_NOW_MAX_DATA_LEN - ROBUSTO_PREFIX_BYTES - 10);
        return send_message_fragmented(peer, robusto_mt_espnow, data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES, ESPNOW_FRAGMENT_SIZE, &esp_now_send_check);
    }

    has_receipt = false;
    int rc = esp_now_send_check(peer, data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES, receipt);
    if (rc != ESP_OK)
    {
        return -ROB_ERR_SEND_FAIL;
    }

    return rc;
}


void espnow_do_on_work_cb(media_queue_item_t *work_item)
{

    ROB_LOGD(espnow_log_prefix, ">> In ESP-NOW work callback.");
    send_work_item(work_item, &(work_item->peer->espnow_info), robusto_mt_espnow, &esp_now_send_message, NULL, espnow_get_queue_context());
}

static esp_err_t espnow_init(void)
{

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
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

void espnow_messaging_init(char *_log_prefix)
{
    espnow_log_prefix = _log_prefix;
    espnow_init();
}

#endif