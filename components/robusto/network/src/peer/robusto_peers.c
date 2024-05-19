
/**
 * @file robusto_peers.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto peer management
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

#include <robusto_network_init.h>
#include <robusto_peer.h>

#include <string.h>
#include <robusto_logging.h>

#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_media.h>
#include <robusto_message.h>
#include <inttypes.h>
#include <stdio.h>

#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include <network/src/media/ble/ble_spp.h>
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
#include <network/src/media/espnow/espnow_peer.h>
#endif

/* Used for creating new peer handles*/
uint16_t _peer_handle_incrementor_ = 0;
struct robusto_peers robusto_peers;

/* The log prefix for all logging */
static char *peers_log_prefix;
   
static callback_new_peer_t *on_new_peer_cb = NULL;

void robusto_register_on_new_peer(callback_new_peer_t *_on_new_peer_cb)
{
    on_new_peer_cb = _on_new_peer_cb;
} 


void robusto_print_peers()
{
    robusto_peer_t *peer;
    ROB_LOGI(peers_log_prefix, "Peer list:"); 
    SLIST_FOREACH(peer, &robusto_peers, next)
    {
       ROB_LOGI(peers_log_prefix, "Name: %s", peer->name);
    }

}

bool notify_on_new_peer(robusto_peer_t *peer) {
    if (on_new_peer_cb) {
        return on_new_peer_cb(peer);
    }
    return true;
}

robusto_peer_t *
robusto_peers_find_peer_by_name(const char *name)
{
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, &robusto_peers, next)
    {

        if (strcmp(peer->name, name) == 0)
        {
            return peer;
        }
    }

    return NULL;
}

robusto_peer_t *
robusto_peers_find_peer_by_handle(int16_t peer_handle)
{
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, &robusto_peers, next)
    {
        if (peer->peer_handle == peer_handle)
        {
            return peer;
        }
    }

    return NULL;
}


#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE

robusto_peer_t *
robusto_peers_find_peer_by_ble_conn_handle(int16_t conn_handle)
{
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, &robusto_peers, next)
    {
        if (peer->ble_conn_handle == conn_handle)
        {
            return peer;
        }
    }

    return NULL;
}
#endif


robusto_peer_t *
robusto_peers_find_duplicate_by_base_mac_address(robusto_peer_t * check_peer)
{
    robusto_peer_t *peer;
    SLIST_FOREACH(peer, &robusto_peers, next)
    {

        if (peer->peer_handle == check_peer->peer_handle) {
            ROB_LOGD(peers_log_prefix, "robusto_peers_find_duplicate_by_base_mac_address, skip self (same handle)");
        } else 
        if (memcmp((uint8_t *)&(peer->base_mac_address), (uint8_t *)&(check_peer->base_mac_address), ROBUSTO_MAC_ADDR_LEN) == 0)
        {
            ROB_LOGD(peers_log_prefix, "robusto_peers_find_duplicate_by_base_mac_address, duplicate found: %s, same as %s, handles (%u/%u)", 
                    peer->name, check_peer->name, peer->peer_handle, check_peer->peer_handle);
            rob_log_bit_mesh(ROB_LOG_DEBUG, peers_log_prefix, peer->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
            return peer;
        } else {
            ROB_LOGD(peers_log_prefix, "robusto_peers_find_duplicate_by_base_mac_address - it is not:"); 
            rob_log_bit_mesh(ROB_LOG_DEBUG, peers_log_prefix, peer->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
        }
    }
    return NULL;
}

robusto_peer_t *
robusto_peers_find_peer_by_base_mac_address(rob_mac_address *mac_address)
{
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, &robusto_peers, next)
    {

        if (memcmp((uint8_t *)&(peer->base_mac_address), mac_address, ROBUSTO_MAC_ADDR_LEN) == 0)
        {
            ROB_LOGD(peers_log_prefix, "robusto_peers_find_peer_by_base_mac_address found:");
            rob_log_bit_mesh(ROB_LOG_DEBUG, peers_log_prefix, mac_address, ROBUSTO_MAC_ADDR_LEN);
            return peer;
        }
    }
    ROB_LOGD(peers_log_prefix, "robusto_peers_find_peer_by_base_mac_address NOT found:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, peers_log_prefix, mac_address, ROBUSTO_MAC_ADDR_LEN);
    return NULL;
}

robusto_peer_t *
robusto_peers_find_peer_by_relation_id_incoming(uint32_t incoming_relation_id)
{
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, &robusto_peers, next)
    {

        if (peer->relation_id_incoming == incoming_relation_id)
        {
            ROB_LOGD(peers_log_prefix, "robusto_peers_find_peer_by_relation_id_incoming found: %lu", incoming_relation_id);

            return peer;
        }
    }
    ROB_LOGI(peers_log_prefix, "robusto_peers_find_peer_by_relation_id_incoming NOT found: %lu", incoming_relation_id);
    return NULL;
}

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
robusto_peer_t *
robusto_peers_find_peer_by_i2c_address(uint8_t i2c_address)
{
    robusto_peer_t *peer;
    ROB_LOGD(peers_log_prefix, "robusto_peers_find_peer_by_i2c_address: %hu", i2c_address);

    SLIST_FOREACH(peer, &robusto_peers, next)
    {
        if (peer->i2c_address == i2c_address)
        {
            return peer;
        }
    }
    ROB_LOGW(peers_log_prefix, "robusto_peers_find_peer_by_i2c_address: I2C peer not found: %hu", i2c_address);
    return NULL;
}
#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
robusto_peer_t *
robusto_peers_find_peer_by_canbus_address(uint8_t canbus_address)
{
    robusto_peer_t *peer;
    ROB_LOGD(peers_log_prefix, "robusto_peers_find_peer_by_canbus_address: %hu", canbus_address);

    SLIST_FOREACH(peer, &robusto_peers, next)
    {
        if (peer->canbus_address == canbus_address)
        {
            return peer;
        }
    }
    ROB_LOGW(peers_log_prefix, "robusto_peers_find_peer_by_canbus_address: CAN bus peer not found: %hu", canbus_address);
    return NULL;
}
#endif

int robusto_peers_delete_peer(uint16_t peer_handle)
{
    robusto_peer_t *peer;

    peer = robusto_peers_find_peer_by_handle(peer_handle);
    if (peer == NULL)
    {
        ROB_LOGW(peers_log_prefix, "robusto_peers_delete_peer: peer handle not found: %hu", peer_handle);
        return ROB_ERR_PEER_NOT_FOUND;
    }

#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
    // If connected,remove BLE peer
    if (peer->ble_conn_handle != 0)
    {
        ble_peer_delete(peer->ble_conn_handle);
    }
#endif

    SLIST_REMOVE(&robusto_peers, peer, robusto_peer, next);

    // TODO: This needs to do more for stats and some of the media types
    robusto_free(peer);

    return 0;
}
/**
 * @brief Add the peer and set all things on a peer level (not media)
 * 
 * @param name 
 * @param new_peer 
 * @return rob_ret_val_t 
 */
rob_ret_val_t robusto_peers_peer_add(const char *name, robusto_peer_t ** new_peer)
{
    if (name != NULL) {
        ROB_LOGI(peers_log_prefix, "robusto_peers_peer_add %s ", name);
    }
    robusto_peer_t *peer;
    if (name != NULL)
    {
        ROB_LOGI(peers_log_prefix, "robusto_peers_peer_add() - looking for the peer named %s.", name);
        /* TODO: Make sure the peer name is unique*/
        peer = robusto_peers_find_peer_by_name(name);
        if (peer != NULL)
        {
            *new_peer = peer;
            ROB_LOGW(peers_log_prefix, "robusto_peers_peer_add() Peer with this name already existed -  %s ", name);
            return -ROB_ERR_PEER_EXISTS;
        }
    }

    peer = robusto_malloc(sizeof(robusto_peer_t));
    if (peer == NULL)
    {
        *new_peer = NULL;
        ROB_LOGE(peers_log_prefix, "robusto_peers_peer_add() - Out of memory!");
        /* Out of memory. */
        return -ROB_ERR_OUT_OF_MEMORY;
    }
    memset(peer, 0, sizeof(robusto_peer_t));
    peer->peer_handle = _peer_handle_incrementor_++;
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT
    peer->next_availability = 0;
    #endif
    peer->relation_id_incoming = 0;
    peer->relation_id_outgoing = 0;
    peer->state = PEER_UNKNOWN;
    peer->on_presentation = NULL;
    if (name == NULL)
    {
        sprintf(peer->name, "UNKNOWN_%" PRIu16 "", peer->peer_handle);
    }
    else
    {
        if (strlen(name) > (CONFIG_ROBUSTO_PEER_NAME_LENGTH -1)) {
            memcpy(&peer->name, name, CONFIG_ROBUSTO_PEER_NAME_LENGTH - 1);
            peer->name[CONFIG_ROBUSTO_PEER_NAME_LENGTH - 1] = 0x00;
        } else {
            strcpy(&peer->name, name);
        }
        
    }


    SLIST_INSERT_HEAD(&robusto_peers, peer, next);
    *new_peer = peer;
    ROB_LOGI(peers_log_prefix, "robusto_peers_peer_add: Added %s", peer->name);

    return ROB_OK;
}


/**
 * @brief Add and initialize a new peer (does not contact it, see add_peer for that)
 *
 * @param peer_name The peer name
 * @param mac_address The mac adress
 * @return robusto_peer* An initialized peer
 */
robusto_peer_t * robusto_add_init_new_peer(const char *peer_name, rob_mac_address *mac_address, robusto_media_types media_types)
{
    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(mac_address);
    if (peer) {
        return peer;
    }
    robusto_peers_peer_add(peer_name, &peer);
    if (peer != NULL)
    {
        // TODO: Note that we have a potentially pointless 6-byte leak here, trust that the mac_address pointer perseveres?
        memcpy((uint8_t *)&(peer->base_mac_address), mac_address, ROBUSTO_MAC_ADDR_LEN);
        peer->supported_media_types = media_types;
        
        ROB_LOGI(peers_log_prefix, "Supported media types %hhu, Mac:", peer->supported_media_types);
        rob_log_bit_mesh(ROB_LOG_INFO, peers_log_prefix, mac_address, ROBUSTO_MAC_ADDR_LEN);

        init_supported_media_types(peer);
        ROB_LOGD(peers_log_prefix, "Peer added: %s", peer->name);

    }
    else
    {
        if (peer_name) {
            ROB_LOGE(peers_log_prefix, "robusto_add_init_new_peer: Failed to add the %s", peer_name);
        } else {
            ROB_LOGE(peers_log_prefix, "robusto_add_init_new_peer: Failed to add new peer.");
        }
        
    }
    return peer;
}

/**
 * @brief Adds a new peer if not existing, locates it via its mac address, and contacts it and exchanges information
 *
 * @param peer_name The name of the peer, if we want to call it something
 * @param mac_address The mac_address of the peer
 * @param media_type The way we want to contact the peer
 * @return Returns a pointer to the peer
 */
robusto_peer_t *add_peer_by_mac_address(char *peer_name, const uint8_t *mac_address, robusto_media_types media_types)
{
    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(mac_address);
    if (peer) {
        return peer;
    }
    peer = robusto_add_init_new_peer(peer_name, mac_address, media_types);
    if (peer->state < PEER_PRESENTING) {
        // TODO: This should be able to handle trying with several media types
        robusto_send_presentation(peer, media_types, false, presentation_add);
    } else {
        ROB_LOGE(peers_log_prefix, "add_peer_by_mac_address: Will not present %s as we are already presenting", peer_name);
    }

    return peer;
}

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C

/**
 * @brief Add and initialize a new peer (does not contact it, see add_peer for that)
 *
 * @param peer_name The peer name
 * @param i2c_address The i2c adress
 * @return robusto_peer* An initialized peer
 */
robusto_peer_t *robusto_add_init_new_peer_i2c(const char *peer_name, const uint8_t i2c_address)
{
    
    robusto_peer_t *peer = NULL;
    robusto_peers_peer_add(peer_name, &peer);
    if (peer != NULL)
    {
        peer->i2c_address = i2c_address;
        peer->supported_media_types = robusto_mt_i2c;
    }
    else
    {
        ROB_LOGE(peers_log_prefix, "Failed to add the %s", peer_name);
        return NULL;
    }

    return peer;
}
/**
 * @brief Adds a new peer, contacts it using I2C and its I2C address it and exchanges information
 * @note To find I2C peers, one has to either loop all 256 addresses or *know* the address, therefor one cannot go by macaddres.
 * Howerver, it could be done, and here is a question on how the network should work in general. As it is also routing..
 * @param peer_name The name of the peer, if we want to call it something
 * @param i2c_address The I2C address of the peer
 * @return Returns a pointer to the peer
 */
robusto_peer_t *add_peer_by_i2c_address(const char *peer_name, uint8_t i2c_address)
{
    // TODO: Add a check for existing peer.
    robusto_peer_t *peer = robusto_add_init_new_peer_i2c(peer_name, i2c_address);
    if (peer->state < PEER_PRESENTING) {
        // TODO: This should be able to handle trying with several media types
        robusto_send_presentation(peer, robusto_mt_i2c, false, presentation_add);
    } else {
        ROB_LOGE(peers_log_prefix, "add_peer_by_i2c_address: Will not present %s as we are already presenting", peer_name);
    }

    return peer;
}

#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS

/**
 * @brief Add and initialize a new peer (does not contact it, see add_peer for that)
 *
 * @param peer_name The peer name
 * @param canbus_address The CAN bus adress
 * @return robusto_peer* An initialized peer
 */
robusto_peer_t *robusto_add_init_new_peer_canbus(const char *peer_name, const uint8_t canbus_address)
{
    
    robusto_peer_t *peer = NULL;
    robusto_peers_peer_add(peer_name, &peer);
    if (peer != NULL)
    {
        peer->canbus_address = canbus_address;
        peer->supported_media_types = robusto_mt_canbus;
    }
    else
    {
        ROB_LOGE(peers_log_prefix, "Failed to add the %s", peer_name);
        return NULL;
    }

    return peer;
}
/**
 * @brief If not added, adds a new peer, contacts it using CAN bus and its CAN bus address it and exchanges information
 * @note To find CAN bus peers, one has to either loop all 256 addresses or *know* the address, therefor one cannot go by macaddres.
 * Howerver, it could be done, and here is a question on how the network should work in general. As it is also routing..
 * @param peer_name The name of the peer, if we want to call it something
 * @param canbus_address The CAN bus address of the peer
 * @return Returns a pointer to the peer
 */
robusto_peer_t *add_peer_by_canbus_address(const char *peer_name, uint8_t canbus_address)
{
    robusto_peer_t *peer = robusto_peers_find_peer_by_canbus_address(canbus_address);
    if (peer) {
        return peer;
    }
    peer = robusto_add_init_new_peer_canbus(peer_name, canbus_address);
    if (peer->state < PEER_PRESENTING) {
        // TODO: This should be able to handle trying with several media types
        robusto_send_presentation(peer, robusto_mt_canbus, false, presentation_add);
    } else {
        ROB_LOGE(peers_log_prefix, "add_peer_by_canbus_address: Will not present %s as we are already presenting", peer_name);
    }

    return peer;
}

#endif


struct robusto_peers *get_peer_list()
{
    return &robusto_peers;
}

int robusto_peers_init(char *_log_prefix)
{
    on_new_peer_cb = NULL;
    peers_log_prefix = _log_prefix;

    /* Free memory first in case this function gets called more than once. */

    robusto_peer_init(peers_log_prefix);
    robusto_presentation_init(_log_prefix);
    return 0;
}
