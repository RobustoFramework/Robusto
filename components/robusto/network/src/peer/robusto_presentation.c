
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
#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
#include "robusto_pubsub_client.h"
#endif
#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER)
#include "robusto_pubsub_server.h"
#endif
#include "string.h"
#include <robusto_time.h>

static char *presentation_log_prefix;

#define PRESENTATION_QUEUE_WAIT_MS (6000U)

#define HI_POS 0
#define PROT_VER_POS 1
#define PROT_VER_MIN_POS 2
#define MEDIA_T_POS 3
#define I2C_ADDR_POS 4
#define CANBUS_ADDR_POS 5
#define REASON_POS 6
#define REL_ID_IN_POS 7
#define MAC_ADDR_POS 11
#define HALOW_MEDIA_HIGH_POS (MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN)

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
    if (msg_len < 0)
    {
        ROB_LOGE(presentation_log_prefix, ">> Failed making presentation message.");
        peer->state = failstate; 
        return ROB_FAIL;
    }
    ROB_LOGD(presentation_log_prefix, ">> Presentation to send:");
    rob_log_bit_mesh(ROB_LOG_DEBUG, presentation_log_prefix, msg, msg_len);
    queue_state *q_state = robusto_malloc(sizeof(queue_state));
    e_media_type supported_mt = get_host_supported_media_types();
    rob_ret_val_t ret_val_flag = ROB_FAIL;
    for (uint16_t media_type = 1; media_type < ROBUSTO_MEDIA_TYPE_LIMIT; media_type = media_type * 2)
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
         * replies are triggered by remote HI messages, so do not let them consume the important queue reserve
        */
        rob_ret_val_t queue_ret = send_message_raw_internal(peer, media_type, msg, msg_len, q_state, false, 
            (reason == presentation_recover) ?  media_qit_recovery : media_qit_normal, 
            0, (robusto_media_types)(peer->supported_media_types & ~media_type), !is_reply || reason == presentation_recover);

        if (queue_ret != ROB_OK)
        {

            ROB_LOGE(presentation_log_prefix, ">> Error queueing presentation: %i %u", queue_ret, (unsigned)media_type);
            ret_val_flag = queue_ret;
        }
        else if (!robusto_waitfor_queue_state(q_state, PRESENTATION_QUEUE_WAIT_MS, &ret_val_flag))
        {
            peer->state = failstate;

            uint8_t q_progress = q_state[0][0];
            bool queue_in_flight = (q_progress == QUEUE_STATE_QUEUED) ||
                                   (q_progress == QUEUE_STATE_RUNNING) ||
                                   (q_progress == QUEUE_STATE_TRYING_MEDIAS);

            if (ret_val_flag == ROB_ERR_TIMEOUT && q_progress == QUEUE_STATE_SUCCEEDED) {
                // Queue completed around the timeout boundary; treat this as success.
                ret_val_flag = ROB_OK;
            }

            if (!is_reply &&
                ret_val_flag != ROB_ERR_TIMEOUT && !queue_in_flight &&
                info->state == media_state_working) {
                set_state(peer, info, media_type, media_state_problem, media_problem_send_problem);
            }
            
            ROB_LOGE(presentation_log_prefix, ">> Failed sending presentation to %s, mt %u, queue state %hhu , reason code: %hi", peer->name, (unsigned)media_type, *(uint8_t *)q_state[0], ret_val_flag);
            if (queue_in_flight) {
                ROB_LOGW(presentation_log_prefix,
                         ">> Presentation timeout while queue state still in-flight (%hhu), keeping media state unchanged",
                         q_progress);
            }
        }
        else {
            
            if (is_reply)
            {
                // We are replying, so we are done, just go back to the previous state.
                peer->state = failstate;

            } else {
                // We are not replying, so wait for the state to change to PEER_KNOWN_INSECURE
                if ((!robusto_waitfor_byte((uint8_t *)&(peer->state), PEER_KNOWN_INSECURE, 1500)))
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
    if (message == NULL || message->peer == NULL || message->binary_data == NULL ||
        message->binary_data_length <= MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN)
    {
        ROB_LOGE(presentation_log_prefix, "<< Got an invalid HI/HIR presentation (missing peer, data, or required fields).");
        return ROB_FAIL;
    }
    uint8_t incoming_protocol_version = message->binary_data[PROT_VER_POS];
    if (incoming_protocol_version > 1U)
    {
        ROB_LOGE(presentation_log_prefix, "<< Unsupported presentation protocol version %u.", incoming_protocol_version);
        return ROB_FAIL;
    }
    uint32_t name_pos = MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN +
                        (incoming_protocol_version == 1U ? 1U : 0U);
    if (message->binary_data_length <= name_pos)
    {
        ROB_LOGE(presentation_log_prefix, "<< Presentation is missing its peer name.");
        return ROB_FAIL;
    }
    uint32_t name_region_length = message->binary_data_length - name_pos;
    const uint8_t *name_start = message->binary_data + name_pos;
    const uint8_t *name_end = memchr(name_start, '\0', name_region_length);
    if ((message->binary_data[HI_POS] != NET_HI && message->binary_data[HI_POS] != NET_HIR) ||
        name_end == NULL || name_end == name_start ||
        name_end != message->binary_data + message->binary_data_length - 1U ||
        (uint32_t)(name_end - name_start) >= CONFIG_ROBUSTO_PEER_NAME_LENGTH)
    {
        ROB_LOGE(presentation_log_prefix, "<< Presentation peer name is not terminated or exceeds the configured limit.");
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
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
        if (!existing_peer->halow_address_valid && message->peer->halow_address_valid)
        {
            memcpy(existing_peer->halow_mac_address, message->peer->halow_mac_address, ROBUSTO_MAC_ADDR_LEN);
            existing_peer->halow_address_valid = true;
        }
#endif
        ROB_LOGW(presentation_log_prefix, "We already had a peer (%s) with the same base_mac_address (handle %u), removing the new one (%u)", existing_peer->name, existing_peer->peer_handle, message->peer->peer_handle );
        robusto_peers_delete_peer(message->peer->peer_handle);
        message->peer = existing_peer;
    }
    /* Set the protocol versions*/
    message->peer->protocol_version = message->binary_data[PROT_VER_POS];
    // TODO: Check protocol version for highest matching protocol version.
    message->peer->min_protocol_version = message->binary_data[PROT_VER_MIN_POS];

    /* Set supported media types*/
    robusto_media_types incoming_media_types = message->binary_data[MEDIA_T_POS];
    if (incoming_protocol_version == 1U)
    {
        incoming_media_types |= (robusto_media_types)message->binary_data[HALOW_MEDIA_HIGH_POS] << 8;
    }
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (message->peer->halow_address_valid)
    {
        incoming_media_types |= robusto_mt_halow;
    }
#endif
    bool new_supported_media_types = message->peer->supported_media_types != incoming_media_types;
    message->peer->supported_media_types = incoming_media_types;

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
        robusto_create_task((TaskFunction_t)&do_presentation_callback, message, "on_presentation_callback", NULL, 0);
    }

    // Store the relation id
    memcpy(&message->peer->relation_id_outgoing, message->binary_data + REL_ID_IN_POS, ROBUSTO_RELATION_LEN);

   
    /* Set the name of the peer (the rest of the message) */
    memset(message->peer->name, 0, sizeof(message->peer->name));
    memcpy(message->peer->name, name_start,
           (size_t)(name_end - name_start) + 1U);

    /* A peer might have gotten or lost supported media */
    if (new_supported_media_types) {
        init_supported_media_types(message->peer);
        ROB_LOGI(presentation_log_prefix, "<< Initiated new or changed supported media types for %s.", message->peer->name);
    }

    // There might be a previous situation where there was a problem, set this media type to working
    robusto_media_t *info = get_media_info(message->peer, message->media_type);
    set_state(message->peer, info, message->media_type, media_state_working, media_problem_none);

    add_relation((uint8_t *)&message->peer->base_mac_address, message->peer->relation_id_incoming,
                 message->peer->relation_id_outgoing, message->peer->supported_media_types
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
                 ,
                 message->peer->i2c_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
                 ,
                 message->peer->canbus_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
                 ,
                 &message->peer->halow_mac_address,
                 message->peer->halow_address_valid
#endif

    );
    // We now know this peer.
    message->peer->state = PEER_KNOWN_INSECURE;

#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER)
    if (message->binary_data[HI_POS] == NET_HI)
    {
        uint32_t removed = robusto_pubsub_server_unsubscribe_peer_from_all(message->peer);
        if (removed > 0U)
        {
            ROB_LOGI(presentation_log_prefix,
                     "Removed %lu stale PubSub subscriptions for new %s session",
                     (unsigned long)removed,
                     message->peer->name);
        }
    }
#endif

#if defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)
    robusto_pubsub_client_recover_peer_subscriptions(message->peer, message->binary_data[REASON_POS]);
#endif
    
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
    uint32_t name_len = strlen((char *)&(get_host_peer()->name));
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    bool include_high_media_mask = (peer->supported_media_types & robusto_mt_halow) != 0;
#else
    bool include_high_media_mask = false;
#endif
    uint16_t data_len = MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN +
                        (include_high_media_mask ? 1U : 0U) + name_len + 1U;
    // TODO: Null termination should be done at reception instead.
    uint8_t *data = robusto_malloc(data_len);
    if (data == NULL)
    {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    data[HI_POS] = is_reply ? NET_HIR : NET_HI;
    /* Set the protocol versions*/
    data[PROT_VER_POS] = include_high_media_mask ? 1U : 0U;
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
    relation_id_incoming = calc_relation_id((uint8_t *)&(peer->base_mac_address), (uint8_t *)&(get_host_peer()->base_mac_address));

    memcpy(data + REL_ID_IN_POS, &relation_id_incoming, ROBUSTO_RELATION_LEN);

    peer->relation_id_incoming = relation_id_incoming;
    memcpy(data + MAC_ADDR_POS, &(get_host_peer()->base_mac_address), ROBUSTO_MAC_ADDR_LEN);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    if (include_high_media_mask)
    {
        data[HALOW_MEDIA_HIGH_POS] = (uint8_t)(get_host_supported_media_types() >> 8);
    }
#endif
    strcpy((char *)data + MAC_ADDR_POS + ROBUSTO_MAC_ADDR_LEN +
           (include_high_media_mask ? 1U : 0U), (char *)&get_host_peer()->name);
    int message_length = robusto_make_multi_message_internal(
        MSG_NETWORK, 0, 0, NULL, 0, data, data_len, msg);
    robusto_free(data);
    return message_length;
}

void robusto_presentation_init(char *_log_prefix)
{
    presentation_log_prefix = _log_prefix;
}
