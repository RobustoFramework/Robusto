
/**
 * @file robusto_presentation.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto peer presentation protocol implementation
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

#include "robusto_message.h"
#include "robusto_logging.h"
#include "robusto_system.h"
#include "robusto_peer.h"
#include "robusto_queue.h"
#include "robusto_qos.h"
#include "string.h"

static char *presentation_log_prefix;

#define HI_POS 0
#define PROT_VER_POS 1
#define PROT_VER_MIN_POS 2
#define MEDIA_T_POS 3
#define I2C_ADDR_POS 4
#define CANBUS_ADDR_POS 5
#define REASON_POS 6
#define REL_ID_IN_POS 7
#define MAC_ADDR_POS 11

void do_presentation_callback(robusto_message_t *message)
{
    if (message->peer->on_presentation)
    {
        message->peer->on_presentation(message->peer, message->binary_data[REASON_POS]);
    }
    robusto_delete_current_task();
}

rob_ret_val_t robusto_send_presentation(robusto_peer_t *peer, robusto_media_types media_types, bool is_reply, e_presentation_reason reason)
{
    if (peer->state == PEER_PRESENTING)
    {
        ROB_LOGE(presentation_log_prefix, "Will not send a presentation as that is already ongoing");
        return ROB_FAIL;
    }
    ROB_LOGW(presentation_log_prefix, ">> Sending a presentation %sto peer %s using %s.", is_reply ? "reply ": "", peer->name, media_type_to_str(media_types));
    rob_log_bit_mesh(ROB_LOG_DEBUG, presentation_log_prefix, peer->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
    // If it is a reply, us failing to present ourselves should not affect the state of the peer
    e_peer_state failstate = peer->state;
    // If it wasn't a reply, it was an unknown peer, set it back to unknown.
    if (!is_reply)
    {
        peer->state = PEER_PRESENTING;
        failstate = PEER_UNKNOWN;
    }
    uint8_t *msg;
    int msg_len = robusto_make_presentation(peer, &msg, is_reply, reason);
    ROB_LOGD(presentation_log_prefix, ">> Presentation to send:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, presentation_log_prefix, msg, msg_len);
    queue_state *q_state = robusto_malloc(sizeof(queue_state));
    e_media_type supported_mt = get_host_supported_media_types();
    rob_ret_val_t ret_val_flag = ROB_FAIL;
    for (uint16_t media_type = 1; media_type < 25; media_type = media_type * 2)
    {
        if ((!(media_types & media_type) || !(supported_mt & media_type)))
        {
            continue;
        }
        robusto_media_t *info = get_media_info(peer, media_type);
        /* Send presentation:
         * no receipt as a reply requires know outgoing id, which is only available after presentation is parsed
         * only send to the mentioned media type
         * if reason is recovery, set that as the type so that the presentation isn't purged from the queue
         * send as important
        */
        rob_ret_val_t queue_ret = send_message_raw_internal(peer, media_type, msg, msg_len, q_state, false, 
            (reason == presentation_recover) ?  media_qit_recovery : media_qit_normal, 
            0, peer->supported_media_types & ~media_type, true);

        if (queue_ret != ROB_OK)
        {

            ROB_LOGE(presentation_log_prefix, ">> Error queueing presentation: %i %i", queue_ret, media_type);
            ret_val_flag = queue_ret;
        }
        else if (!robusto_waitfor_queue_state(q_state, 1000, &ret_val_flag))
        {
            peer->state = failstate;
            if (info->state == media_state_working) {
                set_state(peer, info, media_type, media_state_problem, media_problem_send_problem);
            }
            
            ROB_LOGE(presentation_log_prefix, ">> Failed sending presentation to %s, mt %hhu, queue state %hhu , reason code: %hi", peer->name, media_type, *(uint8_t *)q_state[0], ret_val_flag);
        }
        else {
            
            if (is_reply)
            {
                // We are replying, so we are done, just go back to the previous state.
                peer->state = failstate;

            } else {
                // We are not replying, so wait for the state to change to PEER_KNOWN_INSECURE
                if ((!robusto_waitfor_byte(&(peer->state), PEER_KNOWN_INSECURE, 1500)))
                {
                    peer->state = failstate;
                    ret_val_flag = ROB_ERR_TIMEOUT;
                    ROB_LOGE(presentation_log_prefix, "The peer %s didn't reach PEER_KNOWN_INSECURE state within timeout (state = %u). System will retry later..", peer->name, peer->state);
                    r_delay(1000);
                } else {
                    ret_val_flag = ROB_OK;
                    break;
                }

            } 
        }
    
        if (peer->state > PEER_PRESENTING)
        {
            break;
        }
    }
    if (peer->state < PEER_KNOWN_INSECURE)
    {
        // This is kind of odd, we are in here while presenting or unknown.
        peer->state = failstate;
    }
    robusto_free_queue_state(q_state);
    return ret_val_flag;
}

rob_ret_val_t robusto_handle_presentation(robusto_message_t *message)
{

    // TODO: Here, or before/later, we need to detect if this is fraudulent or spam to prevent hijacking of peers.
    if (message->peer == NULL)
    {
        ROB_LOGE(presentation_log_prefix, "<< Got a HI or HIR-message with information but message->peer is NULL, internal error!");
        return ROB_FAIL;
    }
    ROB_LOGW(presentation_log_prefix, "<< Got a %s-message through %s with information, length %lu.", message->binary_data[HI_POS] == NET_HI ? "HI": "HIR",
             media_type_to_str(message->media_type), message->binary_data_length);

    /* Parse the base MAC address*/
    memcpy(&message->peer->base_mac_address, message->binary_data + MAC_ADDR_POS, ROBUSTO_MAC_ADDR_LEN);
    // Check the special case if there is an existing peer, with the same base address and another handle, if so, populate that instead.
    robusto_peer_t *existing_peer = robusto_peers_find_duplicate_by_base_mac_address(message->peer);
    if (existing_peer)
    {
        ROB_LOGW(presentation_log_prefix, "We already had a peer (%s) with the same base_mac_address (handle %u), removing the new one (%u)", existing_peer->name, existing_peer->peer_handle, message->peer->peer_handle );
        robusto_peers_delete_peer(message->peer->peer_handle);
        message->peer = existing_peer;
    }
    /* Set the protocol versions*/
    message->peer->protocol_version = message->binary_data[PROT_VER_POS];
    // TODO: Check protocol version for highest matching protocol version.
    message->peer->min_protocol_version = message->binary_data[PROT_VER_MIN_POS];

    /* Set supported media types*/
    bool new_supported_media_types = message->peer->supported_media_types != message->binary_data[MEDIA_T_POS];
    message->peer->supported_media_types = message->binary_data[MEDIA_T_POS];

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    /* Set I2C address */
    message->peer->i2c_address = message->binary_data[I2C_ADDR_POS];
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    /* Set CANBUS address */
    message->peer->canbus_address = message->binary_data[CANBUS_ADDR_POS];
#endif
    /* If assigned, call callback with presentation reason*/
    if (message->peer->on_presentation)
    {
        robusto_create_task(&do_presentation_callback, message, "on_presentation_callback", NULL, 0);
    }

    // Store the relation id
    memcpy(&message->peer->relation_id_outgoing, message->binary_data + REL_ID_IN_POS, ROBUSTO_RELATION_LEN);

   
    /* Set the name of the peer (the rest of the message) */
    memcpy(&(message->peer->name), message->binary_data + MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN, message->binary_data_length - MAC_ADDR_POS - ROBUSTO_MAC_ADDR_LEN);

    /* A peer might have gotten or lost supported media */
    if (new_supported_media_types) {
        init_supported_media_types(message->peer);
        ROB_LOGI(presentation_log_prefix, "<< Initiated new or changed supported media types for %s.", message->peer->name);
    }

    // There might be a previous situation where there was a problem, set this media type to working
    robusto_media_t *info = get_media_info(message->peer, message->media_type);
    set_state(message->peer, info, message->media_type, media_state_working, media_problem_none);

    add_relation(&message->peer->base_mac_address, message->peer->relation_id_incoming,
                 message->peer->relation_id_outgoing, message->peer->supported_media_types
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
                 ,
                 message->peer->i2c_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
                 ,
                 message->peer->canbus_address
#endif

    );
    // We now know this peer.
    message->peer->state = PEER_KNOWN_INSECURE;
    
    ROB_LOGI(presentation_log_prefix, "<< Peer %s now more informed.", message->peer->name);
    log_peer_info(presentation_log_prefix, message->peer);
    
    // If its not a response, we need to respond.
    if (message->binary_data[HI_POS] == NET_HI)
    {
        if (notify_on_new_peer(message->peer))
        {
            robusto_send_presentation(message->peer, message->media_type, true, presentation_reply);
        }
        else
        {
            ROB_LOGW(presentation_log_prefix, "Not replying to a peer due to a negative on_new_peer_cb return value.");
        }
    }
    else if (message->binary_data[HI_POS] == NET_HIR)
    {
        
        ROB_LOGD(presentation_log_prefix, "Not replying to a presentation reply.");
    }
    else
    {
        // TODO: Sometimes we are getting 133 as request type for some reason, this needs to be checked
        ROB_LOGE(presentation_log_prefix, "<< ..invalid request type for handle_presentation %hu, this should not happen and is an internal error! Message:", message->binary_data[0]);
        rob_log_bit_mesh(ROB_LOG_ERROR, presentation_log_prefix, message->raw_data, message->raw_data_length);
        return ROB_FAIL;
    }
    return ROB_OK;
}

// TODO: Add positional defines for all

/**
 * @brief Compile a presentation message telling a peer about our abilities
 *
 * @param peer The peer
 * @param msg The message
 * @param is_reply This is a reply, send a HIR-message

 * @return int
 */
int robusto_make_presentation(robusto_peer_t *peer, uint8_t **msg, bool is_reply, e_presentation_reason reason)
{
    ROB_LOGI(presentation_log_prefix, ">> Create a %s-message with information.", is_reply ? "HIR (reply)" : "HI");

    /**
     * TODO: Use this doc
     * Parameters:
     * pv : Protocol version
     * pvm: Minimal supported version
     * host name: The Robusto host name
     * supported  media types: A byte describing the what communication technologies the peer supports.
     * adresses: A list of addresses in the order of the bits in the media types byte.
     */
    uint32_t name_len = strlen(&(get_host_peer()->name));
    uint16_t data_len = MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN + name_len + 1; // + 1 to include null termination.
    // TODO: Null termination should be done at reception instead.
    uint8_t *data = robusto_malloc(data_len);
    data[HI_POS] = is_reply ? NET_HIR : NET_HI;
    /* Set the protocol versions*/
    data[PROT_VER_POS] = ROBUSTO_PROTOCOL_VERSION;
    data[PROT_VER_MIN_POS] = ROBUSTO_PROTOCOL_VERSION_MIN;
    /* Set supported media types*/
    data[MEDIA_T_POS] = get_host_supported_media_types();
    /* Send I2c address */
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    data[I2C_ADDR_POS] = CONFIG_I2C_ADDR;
#else
    data[I2C_ADDR_POS] = 0;
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    data[CANBUS_ADDR_POS] = CONFIG_ROBUSTO_CANBUS_ADDRESS;
#else
    data[CANBUS_ADDR_POS] = 0;
#endif
    /* Provide a reason*/
    data[REASON_POS] = reason;
    // If it is the first communication (HI), we need to calculate the relationid that the peer can reach us with.
    uint32_t relation_id_incoming;
    relation_id_incoming = calc_relation_id(&(peer->base_mac_address), &(get_host_peer()->base_mac_address));

    memcpy(data + REL_ID_IN_POS, &relation_id_incoming, ROBUSTO_RELATION_LEN);

    peer->relation_id_incoming = relation_id_incoming;
    memcpy(data + MAC_ADDR_POS, &(get_host_peer()->base_mac_address), ROBUSTO_MAC_ADDR_LEN);
    strcpy((char *)data + MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN, &get_host_peer()->name);
    return robusto_make_multi_message_internal(MSG_NETWORK, 0, 0, NULL, 0, data, data_len, msg);
}

void robusto_presentation_init(char *_log_prefix)
{
    presentation_log_prefix = _log_prefix;
}
