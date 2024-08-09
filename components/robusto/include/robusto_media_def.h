
/**
 * @file robusto_media_def.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The definition of the media types.
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

#pragma once
#include <robconfig.h>
#include "stdint.h"

#include <robusto_retval.h>
#include <robusto_queue.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FAILURE_RATE_HISTORY_LENGTH 4

    /**
     * @brief These are the available media types
     * The positive values represents the medias supported in Robusto.
     * The negative are internal types, for example a general mock type for testing of the .
     * To communicate which are supported, they are or'ed from the e_media_type enum values
     *
     */

    /* Note: When adding a media type, update the conductor client.*/

    typedef enum e_media_type
    {
        robusto_mt_none = 0U,

        /* Wireless */
        robusto_mt_ble = 1U,
        robusto_mt_espnow = 2U,
        robusto_mt_lora = 4U,

        /* Wired */
        robusto_mt_i2c = 8U,

        robusto_mt_canbus = 16U,
        /* TODO: To be implemented

        robusto_mt_umts = 32U, // TODO: Only implemented as a gateway service.
        robusto_mt_wifi = 64U,    // TODO: On what level should UMTS and wifi be implemented? Neither are direct and both use IP adressing
        // TTL? Onewire? Probably this should be implemented in some way. Or onewire. (perhaps separate for testing connections)
        */
        /* Special types */
        robusto_mt_mock = 128U
    } e_media_type;

    typedef uint8_t robusto_media_types;

    extern const char *str_media_states[4];

    typedef enum
    {
        // The media works and can be used.
        media_state_working = 0,
        // Robusto have recognized a problem, but is not actively trying to recover yet. The media can still be used, although it is voted down.
        media_state_problem = 1,
        // Robusto is trying to recover from a problem, the media cannot be used as that could interfere with recovery.
        media_state_recovering = 2,
        // The media is initiating or has just been added.
        media_state_initiating = 3
    } e_media_state;

    /* The problem that Robusto defines on its level.
    Obviously there are many more kinds of problems on a more technical level,
    but these are the ones that Robusto detects. */

    extern const char *str_media_problems[11];

    typedef enum
    {
        // There is no problem. Media is working.
        media_problem_none,
        // Our communications cannot reache the peer. See last_peer_receive.
        media_problem_cannot_reach,
        // We are not getting any transmissions from the peer
        media_problem_silence,
        // We have a problem with adding messages to the queue of this media
        media_problem_queue_problem,
        // We have a local problem sending, this due to either hardware failure, a bug, or misconfiguration
        media_problem_send_problem,
        // The peer doesn't reach the PEER_KNOWN_INSECURE peer state.
        media_problem_unknown,
        // The transportation medium is having a lot of traffic
        media_problem_high_traffic,
        // The media has problems due to interference (strong signal while having issues)
        media_problem_interference,
        // The media has problems that match patterns recognized from being deliberately jammed. (random transmission with a strong signal)
        media_problem_jammed,
        // The media has lower level, technical problems that aren't enumerated by Robusto
        media_problem_technical,
        // The media does something it shouldn't, something happened that can not happen, or it is nog being used the way it is supposed to.
        media_problem_bug,

    } e_media_problem;

    typedef struct robusto_media
    {
        // The current state of the media
        e_media_state state;
        // If there is a problem, what is it?
        e_media_problem problem;
        // When did the state change?
        uint64_t last_state_change;
        /* Last we received a heartbeat or other communication from the peer */
        uint64_t last_receive;
        /* Last we sent a regular message to the peer */
        uint64_t last_send;
        /* Last we sent a heartbeat to the peer */
        uint64_t last_sent_heartbeat;
        /* Last time the peer received something from us, doubly used as a counter */
        uint64_t last_peer_receive;
        /* Postpone qos, resetting all last to now. Used during long, fragmented transmissions. */
        bool postpone_qos;
        /* Last score */
        float last_score;
        /* Last scored when */
        uint64_t last_score_time;
        /* How many times score has been calculated since reset */
        int16_t score_count;
        // TODO: We really don't need this to be more than bytes. 0-255 is way enough detail.
        /* Length of the history */
        float failure_rate_history[FAILURE_RATE_HISTORY_LENGTH];
        /* The failure rate*/
        float failure_rate;
        /* Number of crc mismatches from the peer */
        uint32_t crc_mismatches;

        /* Supported speed bit/s */
        uint32_t theoretical_speed;
        /* Actual speed bit/s.
        NOTE: Always lower than theoretical, and with small payloads; *much* lower */
        uint32_t actual_speed;

        /* Number of times we have failed sending to a peer since last check */
        uint32_t send_failures;
        /* Number of times we have failed receiving data from a peer since last check */
        uint32_t receive_failures;
        /* Number of times we have succeeded sending to a peer since last check */
        uint32_t send_successes;
        /* Number of times we have succeeed eceiving data from a peer since last check */
        uint32_t receive_successes;

    } robusto_media_t;

    typedef enum
    {
        /* A normal queue item*/
        media_qit_normal = 0,
        /* A heartbeat. Do not retry, dial down the logging, and do not try with other media */
        media_qit_heartbeat = 1,
        /* Recovery messaging. Do not retry, do not try with other media and do not remove from queues during recovery. */
        media_qit_recovery = 2,
    } e_media_queue_item_type;

    typedef struct media_queue_item
    {
        /* The data */
        uint8_t *data;
        /* The length of the data in bytes */
        uint32_t data_length;
        /* The peer */
        struct robusto_peer *peer;
        /* The queue item state */
        queue_state *state;
        /* The queue item type. */
        e_media_queue_item_type queue_item_type;
        /* Media types to exclude */
        e_media_type exclude_media;
        /* If true, we will wait for a receipt for the message. */
        bool receipt;
        /* How many levels have we recursed (trying other medias and so on)*/
        uint8_t depth;
        /* This is an important queue item */
        bool important;
        /* Queue reference */
        STAILQ_ENTRY(media_queue_item)
        items;
    } media_queue_item_t;

    /* Callbacks that handles incoming work items */
    typedef void(media_queue_callback)(media_queue_item_t *work_item);

    /* Callbacks that act as filters on incoming work items */
    typedef int(media_filter_callback)(media_queue_item_t *work_item);

    queue_context_t *ble_get_queue_context();

#ifdef __cplusplus
} /* extern "C" */
#endif