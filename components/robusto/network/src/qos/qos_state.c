
/**
 * @file qos_state.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief The QoS state machine
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


#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "robusto_qos.h"
#include "robusto_qos_init.h"
#include <robusto_peer.h>
#include <robusto_message.h>
#include <robusto_media.h>
#include <robusto_logging.h>
#include <robusto_repeater.h>
#include "qos_recovery.h"

char *qos_state_log_prefix;

void qos_state_cb();
void qos_state_shutdown_cb();

#define HEARTBEAT_DELAY (CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT *CONFIG_ROBUSTO_REPEATER_DELAY_MS)
#define HEARD_FROM_LIMIT (HEARTBEAT_DELAY * 2)
#define LAST_PEER_RECEIVED_LIFESPAN 1

static char qos_state_name[19] = "QoS\x00";
on_state_change_t *cb_on_state_change = NULL;
recurrence_t qos_state = {
    recurrence_name : &qos_state_name,
    skip_count : CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT,
    skips_left : 0,
    recurrence_callback : &qos_state_cb,
    shutdown_callback : &qos_state_shutdown_cb
};

// Function prototypes
void transition_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void working_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void problem_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void recovering_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void down_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);
void any_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type);



void robusto_qos_register_on_state_change(on_state_change_t *_cb_on_state_change) {
    cb_on_state_change = _cb_on_state_change;
}

void set_peer_problematic_media_types(robusto_peer_t * peer, e_media_type problematic_media_type) {
    peer->problematic_media_types |= problematic_media_type;
}

void unset_peer_problematic_media_types(robusto_peer_t * peer, e_media_type unproblematic_media_type) {
    peer->problematic_media_types &= ~unproblematic_media_type;
}

void set_state(robusto_peer_t * peer, robusto_media_t *info, e_media_type media_type, e_media_state media_state, e_media_problem problem)
{
    if (info->state == media_state) {
        // If there is no change to the state, there is no state change.
        return;
    }

    info->state = media_state;
    info->last_state_change = r_millis();
    info->problem = problem;
    if (media_state > media_state_working) {
        set_peer_problematic_media_types(peer, media_type);
    } else {
        unset_peer_problematic_media_types(peer, media_type);
    }
    if (cb_on_state_change) {
        cb_on_state_change(peer, info, media_type, media_state, problem);
    }
    ROB_LOGW(qos_state_log_prefix, "State change, state %u, problem %u", media_state, problem);
}

rob_ret_val_t peer_level_check(robusto_peer_t *peer) {

    // All media types have problems for this peer, we assume that we have been forgotten
    if (peer->supported_media_types == peer->problematic_media_types) {
        if ((peer->state != PEER_UNKNOWN) && (peer->state != PEER_PRESENTING)) {
            ROB_LOGW(qos_state_log_prefix, "All medias have problems for peer %s, we may be forgotten, trigger presentation.", peer->name);
            // This will make the peer send another presentation
            peer->state = PEER_UNKNOWN;
            // TODO: Check that we haven't heard from the peer for a while.
        }
    }  
    return ROB_OK; 
}

rob_ret_val_t check_peer(robusto_peer_t *peer)
{

    robusto_media_t *info;
    uint64_t last_heartbeat_time;
    if ((int)r_millis() - HEARTBEAT_DELAY - 100 < 0) {
        // Some platforms are so fast, that we end up in here too quickly, let's wait some more, no point doing these things during boot.
        return ROB_OK;
    }
    // Loop all media types and check each
    for (e_media_type media_type = 1; media_type < 256; media_type = media_type * 2)
    {
        if (
            !(
                ((peer->supported_media_types & media_type) == media_type) && 
                ((get_host_peer()->supported_media_types & media_type) == media_type)
                )
            )
        {
            continue;
        }
        info = get_media_info(peer, media_type);

        last_heartbeat_time = r_millis() - HEARTBEAT_DELAY -100;
        ROB_LOGD(qos_state_log_prefix, "In check_peer() media_type %hhu, state %hhu", media_type, info->state);

        // State machine transitions
        transition_state(peer, info, last_heartbeat_time, media_type);
    
        // State-specific actions
        switch (info->state)
        {
        case media_state_working:
            working_state(peer, info, last_heartbeat_time, media_type);
            break;
        case media_state_problem:
            problem_state(peer, info, last_heartbeat_time, media_type);
            break;
        case media_state_recovering:
            recovering_state(peer, info, last_heartbeat_time, media_type);
            break;
        case media_state_down:
            down_state(peer, info, last_heartbeat_time, media_type);
            break;
        default:
            break;
        }

        peer_level_check(peer);

        any_state(peer, info, last_heartbeat_time, media_type);
    }

    return 0;
}


void check_media(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{
    // Note: The order of these checks are important as they might hide each other

    if ((last_heartbeat_time > HEARD_FROM_LIMIT) && (info->last_send < (last_heartbeat_time - HEARD_FROM_LIMIT)))
    {
        ROB_LOGE(qos_state_log_prefix, "Internal issue: We have problems sending to the peer %s using the media type %s. Last success: %llu. \n\tlast_heartbeat_time: %llu, info->last_state_change %llu.",
                 peer->name, media_type_to_str(media_type), info->last_send, last_heartbeat_time, info->last_state_change);

        if (info->state != media_state_down)
        {
            set_state(peer, info, media_type, media_state_problem, media_problem_send_problem);
        }
    }
    else if ((last_heartbeat_time > HEARD_FROM_LIMIT) && (info->last_receive < last_heartbeat_time - HEARD_FROM_LIMIT))
    {

        ROB_LOGW(qos_state_log_prefix, "The peer %s and media type %s has not been heard from since (last_receive): %llu. last_heartbeat_time: %llu, info->last_state_change %llu. Mac:",
                 peer->name, media_type_to_str(media_type), info->last_receive, last_heartbeat_time, info->last_state_change);
        rob_log_bit_mesh(ROB_LOG_WARN, qos_state_log_prefix, &(peer->base_mac_address), 6);                 
        if (info->state != media_state_down)
        {
            set_state(peer, info, media_type, media_state_problem, media_problem_silence);
        }
    }
    else
    {
        if ((peer->state != PEER_UNKNOWN) && (info->state != media_state_working))
        {
            
            ROB_LOGW(qos_state_log_prefix, "The peer %s, media type %s, state %u problem %u seem to be fixed now => changing state to working, info->last_state_change %llu.",
                     peer->name, media_type_to_str(media_type), info->state, info->problem, info->last_state_change);
            set_state(peer, info, media_type, media_state_working, media_problem_none);
        }
    }

    if ((last_heartbeat_time > 100) && (info->last_peer_receive > 0) && 
    (last_heartbeat_time > HEARD_FROM_LIMIT) && 
    (info->last_peer_receive < (last_heartbeat_time - HEARD_FROM_LIMIT)))
    {

        ROB_LOGW(qos_state_log_prefix, "The peer %s and media type %s has not heard from us since (info->last_peer_receive): %llu. \n\t, last_heartbeat_time: %llu, info->last_receive %llu info->last_state_change %llu.%llu",
                 peer->name, media_type_to_str(media_type), info->last_peer_receive, last_heartbeat_time, info->last_receive, info->last_state_change, (int64_t)(last_heartbeat_time - (HEARTBEAT_DELAY * 4)));
        // This is not always sent, so we also use it as a counter
        if (info->last_peer_receive > LAST_PEER_RECEIVED_LIFESPAN) {
            info->last_peer_receive = LAST_PEER_RECEIVED_LIFESPAN;
        } else {
            info->last_peer_receive--;
        }       
        // TODO: While this should not cause a problem state, just that it is here, can cause a problem to not resolve.
        /* 
        if (info->state != media_state_down)
        {
            set_state(peer, info, media_type, media_state_problem, media_problem_cannot_reach);
        }
        */

    }


}

//
/**
 * @brief Transition between communication states based on received data.
 *
 * @param peer The peer
 * @param info The info structure
 * @param last_delay The delay to the last time
 * @param media_type Convenient for the function
 */
void transition_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{

    switch (info->state)
    {
    case media_state_working:
        check_media(peer, info, last_heartbeat_time, media_type);

        break;
    case media_state_problem:

        check_media(peer, info, last_heartbeat_time, media_type);

        break;
    case media_state_recovering:

        break;
    case media_state_down:
        check_media(peer, info, last_heartbeat_time, media_type);

    default:
        break;
    }

}

// Any state behavior
void any_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{
    // Perform actions that should be done in any state.

    // If the peer hasn't reached a known state and isn't down or recovering, send a presentation using this (working) media
    if (peer->state == PEER_UNKNOWN) 
    {
        ROB_LOGE(qos_state_log_prefix, "The peer %s is UNKNOWN, will send a presentation using %s.",
                 peer->name, media_type_to_str(media_type));
        robusto_send_presentation(peer, media_type, false);
    }
    // TODO: react on PEER_KNOWN_SUSPECT and PEER_BANNED
}

// Working state behavior
void working_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{
    // Perform working state actions

}

// Problem state behavior
void problem_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{

    if (info->last_state_change < last_heartbeat_time + 10)
    {
        ROB_LOGE(qos_state_log_prefix, "%s, media type %s has had a problem for too long, going into recovery.", peer->name, media_type_to_str(media_type));
        set_state(peer, info, media_type, media_state_recovering, info->problem);
        recover_media(peer, info, last_heartbeat_time, media_type);
    }
    else if (info->last_state_change < r_millis() - 100)
    {
        check_media(peer, info, last_heartbeat_time, media_type);
    }
}

// Recovering state behavior
void recovering_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{
    // Has it been recovering more than one cycle, mark it as down.
    if (info->last_state_change < last_heartbeat_time + 10)
    {
        ROB_LOGE(qos_state_log_prefix, "%s, media type %s hasn't recovered, setting to down. info->last_state_change %llu, last_heartbeat_time %llu",
                 peer->name, media_type_to_str(media_type), info->last_state_change, last_heartbeat_time);
        set_state(peer, info, media_type, media_state_down, info->problem);
    }
}

// Down state behavior
void down_state(robusto_peer_t *peer, robusto_media_t *info, uint64_t last_heartbeat_time, e_media_type media_type)
{
     ROB_LOGE(qos_state_log_prefix, "%s, media type %s is still down. info->last_state_change %llu, last_heartbeat_time %llu",
                 peer->name, media_type_to_str(media_type), info->last_state_change, last_heartbeat_time);
}

void qos_state_cb()
{
    ROB_LOGD(qos_state_log_prefix, "In qos_state_cb()");
    robusto_peer_t *peer;

    SLIST_FOREACH(peer, get_peer_list(), next)
    {
        ROB_LOGI(qos_state_log_prefix, "Checking peer: %s", peer->name);
        check_peer(peer);
    }
}

void qos_state_shutdown_cb()
{
}

void init_qos_state(char *_log_prefix)
{
    qos_state_log_prefix = _log_prefix;
    robusto_register_recurrence(&qos_state);
}