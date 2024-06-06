/**
 * @file robusto_peer.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Provides the Robusto peer definitions
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
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

#pragma once

#include <robconfig.h>

#include <robusto_sys_queue.h>

#include <stdint.h>
#include <stdbool.h>
#include <robusto_system.h>
#include <robusto_media_def.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef CONFIG_ROBUSTO_PEER_NAME
#define CONFIG_ROBUSTO_PEER_NAME 16
#endif

#ifndef CONFIG_ROBUSTO_PEER_NAME
#define CONFIG_ROBUSTO_PEER_NAME "TEST"
#endif

    /* Robusto peer state, the security state */
    // TODO: This should be called security state or something else instead.
    typedef enum e_peer_state
    {

        /* The peer is unknown; we are yet to present us to them */
        PEER_UNKNOWN = 0U,
        /* We are presenting us to a peer, awaiting response. Do not send another presentation. */
        PEER_PRESENTING = 1U,
        /* The peer has presented itself, but isn't encrypted*/
        PEER_KNOWN_INSECURE = 2U,
        /* The peer is both known and encrypted */
        PEER_KNOWN_SECURE = 3U,
        /* The peer has behaved in a suspect manner */
        /* TODO: This is not probably not correct to have here, this is a connection level problem */
        PEER_KNOWN_SUSPECT = 4U,
        /* The peer has been banned. */
        PEER_BANNED = 5U,

    } e_peer_state;

/* MAC-addresses should always be 6-byte values on ESP32. TODO: Probably at all times? */
#ifdef USE_ESPIDF
#if (ESP_NOW_ETH_ALEN && ESP_NOW_ETH_ALEN != 6) || ROBUSTO_MAC_ADDR_LEN != 6
#error ESP_NOW_ETH_ALEN or ROBUSTO_MAC_ADDR_LEN has been set to something else than six bytes. \
MAC-addresses are 6-byte values regardless of technology. \
This library assumes this and may fail using other lengths for this setting.
#endif
#endif

/* This is the maximum number of peers */
/* NOTE: A list of relations, it relations+mac_addresses (4 + 6 * ROBUSTO_MAX_PEERS) is stored
 * in RTC memory, so 32 peers mean 320 bytes of the 8K RTC memory
 */
#define ROBUSTO_MAX_PEERS 32

    /* Presentation */

    typedef enum
    {
        /* We added this peer */
        presentation_add = 0x00,
        /* We just awoke from sleep */
        presentation_wake = 0x01,
        /* We just rebooted */
        presentation_reboot = 0x02,
        /* We are trying to recover the connection */
        presentation_recover = 0x03,
        /* We want to update some details */
        presentation_update = 0x04,
        /* We are replying to your presentation */
        presentation_reply = 0x05,

    } e_presentation_reason;

    /* The SD MAC-address type */
    typedef uint8_t rob_mac_address[ROBUSTO_MAC_ADDR_LEN];

    typedef struct robusto_peer robusto_peer_t;
    /* Callback used when peers */
    typedef void(on_presentation_cb_t)(robusto_peer_t *peer, e_presentation_reason reason);

    /* A peer name in Robusto */
    typedef char rob_peer_name[CONFIG_ROBUSTO_PEER_NAME_LENGTH];
    /* A peer */
    typedef struct robusto_peer
    {
        /* The name of the peer*/
        rob_peer_name name;

        /**
         * @brief Following is the the 6-byte base MAC adress of the peer.
         * Note that the other MAC-adresses are offset, and in BLE´s case little-endian, for example. More info here:
         * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/misc_system_api.html#mac-address
         */
        rob_mac_address base_mac_address;

        /**
         * @brief  If set, called when a peer is re-presenting itself to us.
         * @note As sending a presentation is a common action by a peer, basically done in most cases when there is a problem, the cause of the contact can be quite important to know.
         */
        on_presentation_cb_t *on_presentation;

        /* Supported media types, one bits for each */
        robusto_media_types supported_media_types;

        /* Problematic media types, one bits for each. If equal to supported, all have problems. */
        robusto_media_types problematic_media_types;
        /** A generated 32-bit crc32 of the remote peer and this peers mac addresses
         * Used by non-addressed and low-bandwith medias (LoRa) to economically resolve peers
         */
        uint32_t relation_id_incoming;
        uint32_t relation_id_outgoing;
        /* The locally unique handle of the peer*/
        uint16_t peer_handle;
        /* The peer state, if unknown, it cannot be used in many situations*/
        e_peer_state state;
        /* Protocol version*/
        uint8_t protocol_version;
        /* Minimum supported protocol version*/
        uint8_t min_protocol_version;
#ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT
        /* Next availability (measured in milliseconds from boot)*/
        uint32_t next_availability;
#endif
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
        /* This client will likely go to sleep (matters for QoS).
        This will be set on reboot for all remembered peers. */
        bool sleeper;
#endif
/* Media-specific statistics, used by transmission optimizer */
#if defined(CONFIG_ROBUSTO_SUPPORTS_BLE) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t ble_info;
        /* The connection handle of the BLE connection, NimBLE has its own peer handling.*/
        int ble_conn_handle;
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t espnow_info;
#endif

#if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t canbus_info;
        uint8_t canbus_address;
#endif

#if defined(CONFIG_ROBUSTO_SUPPORTS_LORA) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t lora_info;
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_I2C) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t i2c_info;
        uint8_t i2c_address;
#endif
#if defined(CONFIG_ROBUSTO_NETWORK_MOCK_TESTING) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
        robusto_media_t mock_info;
#endif
        SLIST_ENTRY(robusto_peer)
        next;

    } robusto_peer_t;




    int robusto_make_presentation(robusto_peer_t *peer, uint8_t **msg, bool is_reply, e_presentation_reason reason);
    rob_ret_val_t robusto_send_presentation(robusto_peer_t *peer, robusto_media_types media_types, bool is_reply, e_presentation_reason reason);

    robusto_media_types get_host_supported_media_types();
    void add_host_supported_media_type(e_media_type supported_media_type);

    robusto_peer_t *get_host_peer();

    typedef bool(callback_new_peer_t)(robusto_peer_t *peer);

#ifdef __cplusplus
} /* extern "C" */
#endif