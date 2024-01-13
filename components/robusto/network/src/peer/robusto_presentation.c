
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

static char * presentation_log_prefix;



rob_ret_val_t robusto_send_presentation(robusto_peer_t *peer, e_media_type media_type, bool is_reply) {
    if (peer->state == PEER_PRESENTING) {
        ROB_LOGE(presentation_log_prefix, "Will not send a presentation as that is already ongoing");
        return ROB_FAIL;
    }
    ROB_LOGI(presentation_log_prefix, ">> Sending a presentation to peer %s using %s.", peer->name, media_type_to_str(media_type));
    // If it is a reply, us failing to present ourselves should not affect the state of the peer
    e_peer_state failstate = peer->state;
    // If it wasn't a reply, it was an unknown peer, set it back to unknown.
    if (!is_reply) {
        peer->state = PEER_PRESENTING;
        failstate = PEER_UNKNOWN;
    }
    uint8_t *msg;
    int msg_len = robusto_make_presentation(peer, &msg, is_reply);
    ROB_LOGD(presentation_log_prefix, ">> Presentation to send:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, presentation_log_prefix, msg, msg_len);
    queue_state *q_state = robusto_malloc(sizeof(queue_state));
    rob_ret_val_t ret_val_flag;
    robusto_media_t *info = get_media_info(peer, media_type);
    // Send presentation (no receipt as a reply requires know outgoing id, which is only available after presentation is parsed)
    rob_ret_val_t queue_ret = send_message_raw(peer, media_type, msg, msg_len, q_state, false);
    if (queue_ret != ROB_OK) {
        peer->state = failstate;
        ROB_LOGE(presentation_log_prefix, ">> Error queueing presentation: %i %i", queue_ret, media_type);
        ret_val_flag = queue_ret;
    } else
    if (!robusto_waitfor_queue_state(q_state, 6000, &ret_val_flag)) {
        peer->state = failstate;
        set_state(peer, info, media_type, media_state_problem, media_problem_send_problem);
        ROB_LOGE(presentation_log_prefix, ">> Failed sending presentation to %s, mt %hhu, queue state %hhu , reason code: %hi", peer->name, media_type, *(uint8_t*)q_state[0], ret_val_flag);
    } else  
    // We are presenting, so wait for the state to change to PEER_KNOWN_INSECURE
    if (!is_reply && (!robusto_waitfor_byte(&(peer->state), PEER_KNOWN_INSECURE, 10000))) {
        peer->state = failstate;
        ret_val_flag = ROB_ERR_TIMEOUT;
        ROB_LOGE(presentation_log_prefix, "The peer %s didn't reach PEER_KNOWN_INSECURE state within timeout. System will retry later..", peer->name);
    }

    robusto_free_queue_state(q_state);
    return ret_val_flag;
}

rob_ret_val_t robusto_handle_presentation(robusto_message_t *message)
{
    
    // TODO: Here, or before/later, we need to detect if this is fraudulent or spam.

    if (message->peer == NULL)
    {
        ROB_LOGE(presentation_log_prefix, "<< Got a HI or HIR-message with information but message->peer is NULL, internal error!");
        return ROB_FAIL;
    }
    ROB_LOGW(presentation_log_prefix, "<< Got a HI or HIR-message through %s with information, length %lu.", 
        media_type_to_str(message->media_type), message->binary_data_length);

    message->peer->state = PEER_PRESENTING;
    /* Parse the base MAC address*/
    if (memcmp(&message->peer->base_mac_address, message->binary_data + 9, ROBUSTO_MAC_ADDR_LEN) != 0)
    {
        memcpy(&message->peer->base_mac_address, message->binary_data + 9, ROBUSTO_MAC_ADDR_LEN);
    }
    // Check if there is already an existing peer, if so, populate that instead.
    robusto_peer_t * existing_peer = robusto_peers_find_duplicate_by_base_mac_address(message->peer);
    if (existing_peer) {
        ROB_LOGW(presentation_log_prefix, "We already had a peer (%s) with the same base_mac_address, removing the new one", existing_peer->name);
        robusto_peers_delete_peer(message->peer->peer_handle);
        message->peer = existing_peer;
    }
    /* Set the protocol versions*/
    message->peer->protocol_version = message->binary_data[1];
    // TODO: Check protocol version for highest matching protocol version.
    message->peer->min_protocol_version = message->binary_data[2];

    /* Set supported media types*/
    message->peer->supported_media_types = message->binary_data[3];

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    message->peer->i2c_address = message->binary_data[4];
#endif
    // Store the relation id
    memcpy(&message->peer->relation_id_outgoing, message->binary_data + 5, 4);

    init_supported_media_types(message->peer);
    ROB_LOGI(presentation_log_prefix, "<< Initiated all supported media types.");

    /* Set the name of the peer (the rest of the message) */
    memcpy(&(message->peer->name), message->binary_data + 9 + ROBUSTO_MAC_ADDR_LEN, message->binary_data_length - 9 - ROBUSTO_MAC_ADDR_LEN);
    message->peer->state = PEER_KNOWN_INSECURE;

    // There might be a previous situation where there was a problem, set this media type to working
    robusto_media_t *info = get_media_info(message->peer, message->media_type);
    set_state(message->peer, info, message->media_type, media_state_working, media_problem_none);
    

    add_relation(&message->peer->base_mac_address, message->peer->relation_id_incoming, 
    message->peer->relation_id_outgoing, message->peer->supported_media_types
    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        , message->peer->i2c_address
    #endif
        );
    ROB_LOGI(presentation_log_prefix, "<< Peer %s now more informed.", message->peer->name);
    log_peer_info(presentation_log_prefix, message->peer);

    // If its not a response, we need to respond.
    if (message->binary_data[0] == NET_HI)
    {   
        if (notify_on_new_peer(message->peer))
        {
            robusto_send_presentation(message->peer, message->media_type, true);
        }
        else
        {
            ROB_LOGW(presentation_log_prefix, "Not replying to a peer due to a negative on_new_peer_cb return value.");
        }
    } else if (message->binary_data[0] == NET_HIR) {
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

/**
 * @brief Compile a presentation message telling a peer about our abilities
 *  
 * @param peer The peer
 * @param msg The message
 * @param is_reply This is a reply, send a HIR-message

 * @return int 
 */
int robusto_make_presentation(robusto_peer_t *peer, uint8_t **msg, bool is_reply)
{
    ROB_LOGI(presentation_log_prefix, ">> Create a %s-message with information.", is_reply ? "HIR (reply)": "HI");

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
    
    uint16_t data_len = 9 + ROBUSTO_MAC_ADDR_LEN + name_len + 1; // + 1 to include null termination. 
    //TODO: Null termination should be done at reception instead.
    uint8_t *data = robusto_malloc(data_len);
    data[0] = is_reply ? NET_HIR : NET_HI;
    /* Set the protocol versions*/
    data[1] = ROBUSTO_PROTOCOL_VERSION;
    data[2] = ROBUSTO_PROTOCOL_VERSION_MIN;
    /* Set supported media types*/
    data[3] = get_host_supported_media_types();
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    data[4] = CONFIG_I2C_ADDR;
#else
    data[4] = 0;
#endif
    // If it is the first communication (HI), we need to calculate the relationid that the peer can reach us with.
    uint32_t relation_id_incoming;
    relation_id_incoming = calc_relation_id(&(peer->base_mac_address), &(get_host_peer()->base_mac_address));

    memcpy(data + 5, &relation_id_incoming, 4);
    
    peer->relation_id_incoming = relation_id_incoming;
    memcpy(data + 9, &(get_host_peer()->base_mac_address), ROBUSTO_MAC_ADDR_LEN);
    strcpy((char *)data + 9 + ROBUSTO_MAC_ADDR_LEN, &get_host_peer()->name);
    return robusto_make_binary_message(MSG_NETWORK, 0, 0, data, data_len, msg);
}

void robusto_presentation_init(char *_log_prefix)
{
    presentation_log_prefix = _log_prefix;

}
