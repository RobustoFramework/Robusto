/**
 * @file robusto_peer.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Provides the Robusto peer interface
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


#ifndef USE_ARDUINO
#include "sys/queue.h"
#else
#include <compat/arduino_sys_queue.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <robusto_system.h>
#include <robusto_media_def.h>
#include <robusto_peer_def.h>

#ifdef __cplusplus
extern "C"
{
#endif


    /**
     * @brief Sets a callback that will be called when a peer presentation has been received
     *
     * @param _on_inform_peer_cb A pointer to the function to call
     */
    void robusto_register_on_new_peer(callback_new_peer_t *_on_new_peer_cb);

    /**
     * @brief Sends a notification that a new peer has been added
     *
     * @param peer The new peer
     * @return true If the peer should be added or no callback has beend register
     * @return false If the peer should not be added
     */
    bool notify_on_new_peer(robusto_peer_t *peer);
    void robusto_peer_init_peer(robusto_peer_t *peer);
    void robusto_print_peers();
    void init_supported_media_types(robusto_peer_t *peer);

    void log_peer_info(char *_log_prefix, robusto_peer_t *peer);

    robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type media_type);
    float score_peer(robusto_peer_t *peer, e_media_type media_type, int data_length);
    rob_ret_val_t set_suitable_media(robusto_peer_t *peer, uint16_t data_length, e_media_type exclude, e_media_type *result);

    void peer_stat_reset(robusto_media_t *stats);
    /**
     * @brief Wait for a peer to at least reach the specified state
     * 
     * @param state The state
     * @param timeout Timeout in milliseconds
     */
    bool peer_waitfor_at_least_state(robusto_peer_t * peer, e_peer_state state, uint32_t timeout);

    void robusto_peer_init(char *_log_prefix);

    /* -------------------------- The peer list ---------------------------------------- */

    /**
     * @brief Construct the peer list
     * Declare here for convenience, because some loop robusto_peers.
     * TODO: Perhaps this should be hidden, should probably be a mutex lock?
     */

    SLIST_HEAD(robusto_peers, robusto_peer);

    int robusto_peers_delete_peer(uint16_t peer_handle);
    rob_ret_val_t robusto_peers_peer_add(const char *name, robusto_peer_t **new_peer);

    /* Initialization */

    int robusto_peers_init(char *_log_prefix);
    void robusto_presentation_init(char *_log_prefix);
    robusto_peer_t *add_peer_by_mac_address(char *peer_name, const uint8_t *mac_address, e_media_type media_type);
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    robusto_peer_t *add_peer_by_i2c_address(const char *peer_name, uint8_t i2c_address);
    robusto_peer_t *robusto_add_init_new_peer_i2c(const char *peer_name, const uint8_t i2c_address);
#endif
    robusto_peer_t *robusto_add_init_new_peer(const char *peer_name, rob_mac_address *mac_address, e_media_type media_type);

    struct robusto_peers *get_peer_list();

    /* Lookups*/
    robusto_peer_t *robusto_peers_find_peer_by_name(const char *name);
    robusto_peer_t *robusto_peers_find_peer_by_handle(int16_t peer_handle);
    robusto_peer_t *robusto_peers_find_peer_by_i2c_address(uint8_t i2c_address);
    /**
     * @brief Return any duplicate peers (excluding the peer itself)
     *
     * @param check_peer The peer to check
     * @return robusto_peer_t* The matching peer
     */
    robusto_peer_t *robusto_peers_find_duplicate_by_base_mac_address(robusto_peer_t *check_peer);
    /**
     * @brief Find peer by base MAC address
     *
     * @param mac_address The MAC address
     * @return robusto_peer_t* The matching peer
     */
    robusto_peer_t *robusto_peers_find_peer_by_base_mac_address(rob_mac_address *mac_address);
    /**
     * @brief Find peer by its incoming relation id
     *
     * @param incoming_relation_id The relation id it uses to reach us
     * @return robusto_peer_t* The matching peer
     */
    robusto_peer_t *robusto_peers_find_peer_by_relation_id_incoming(uint32_t incoming_relation_id);

    /**
     * @brief A relation is the distilled technical information needed to communicate with a peer
     * Among other thing used for storing peer information over resets.
     *
     */

    typedef struct relation
    {
        uint32_t relation_id_incoming;
        uint32_t relation_id_outgoing;
        uint8_t mac_address[ROBUSTO_MAC_ADDR_LEN];
        uint8_t supported_media_types;
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        uint8_t i2c_address;
#endif
    } relation_t;

    /**
     * @brief Calculate the relation id as a CRC32 hash of two six-byte MAC-addresses
     *
     * @param mac_1 Pointer to first MAC address
     * @param mac_2 Pointer to second MAC address
     * @return uint32_t The calculated CRC32 relationid
     */
    uint32_t calc_relation_id(uint8_t *mac_1, uint8_t *mac_2);
    /**
     * @brief Find the relation id through the mac address.
     * (without having to loop all peers)
     * @param relation_id The relation id to investigate
     * @return uint8_t*
     */
    rob_mac_address *relation_id_incoming_to_mac_address(uint32_t relation_id);
    /**
     * @brief The number of relations
     *
     * @return uint8_t
     */
    uint8_t get_relation_count();

    /**
     * @brief After a reset, this can be called to recreate peers form stored relations
     *
     */
    void recover_relations();
    /**
     * @brief Add a relation to the list of relations.
     * @note There is a hard limit to the number of relations set in ROBUSTO_MAX_PEERS.
     * 
     */
    bool add_relation(uint8_t *mac_address, uint32_t relation_id_incoming, uint32_t relation_id_outgoing, uint8_t supported_media_types
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
                      ,
                      uint8_t i2c_address
#endif
    );

#ifdef __cplusplus
} /* extern "C" */
#endif