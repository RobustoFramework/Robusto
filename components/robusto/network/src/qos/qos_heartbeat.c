
/**
 * @file qos_heartbeat.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto hearbeats implementation
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

#include "robusto_qos.h"
#include "robusto_qos_init.h"
#include <robusto_media.h>
#include <robusto_repeater.h>
#include <robusto_peer.h>
#include <robusto_time.h>
#include <robusto_message.h>
#include <robusto_logging.h>
#include <string.h>


#define UNACCEPTABLE_SCORE -90
/* The margin for which to wait before considering the peer/media to be idle*/
#define HEARTBEAT_IDLE_MARGIN_MS (CONFIG_ROBUSTO_REPEATER_DELAY_MS * CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT) + 100
#define HEARTBEAT_PROBLEM_MARGIN_MS (CONFIG_ROBUSTO_REPEATER_DELAY_MS * CONFIG_ROBUSTO_PEER_HEARTBEAT_PROBLEM_SKIP_COUNT) 

static char *heartbeat_log_prefix;

void heartbeat_cb();
void heartbeat_shutdown_cb();

// TODO: Add unit tests for this code.

recurrence_t heartbeat = {
    recurrence_name : "Heartbeats",
    skip_count : CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT,
    skips_left : 0,
    recurrence_callback : &heartbeat_cb,
    shutdown_callback : &heartbeat_shutdown_cb
};
/**
 * @brief Calculate, in tenths of milliseconds, the time since the provided time.
 * @note It is in tenths as that is the default precision of the repeater and provides the most data with the least bytes.
 * @param since Since when to calculate.
 * @return uint16_t The time in tenths of milliseconds, will be UINT16_MAX if more than that.
 */
uint16_t calc_deka_ms_since(uint64_t since, uint64_t curr_time)
{
    uint64_t diff = curr_time - since;
    uint16_t retval;
    if (diff > UINT16_MAX)
    {
        retval = UINT16_MAX;
    }
    else
    {
        retval = (uint16_t) (diff / 10);
    }
    ROB_LOGD(heartbeat_log_prefix, "calc_deka_ms_since returns %02X %02X, %i (diff = %llu since = %llu, curr_time = %llu ", ((uint8_t *)&retval)[0], ((uint8_t* )&retval)[1],retval, diff, since, curr_time);
    return retval;
}

uint64_t parse_heartbeat(uint8_t *data, uint8_t preamble_len)
{
    // TODO: It seems like this can happen before initialization
    uint16_t *retval = data + preamble_len;
    uint64_t hb_time = r_millis() - (*retval * 10);
    // TODO: We need a multistage boot process that differs from initialisation and startup or whatevs. Runlevels?
    ROB_LOGD("Heartbeat", "parse_heartbeat data %02X %02X since = %i calculated time = %llu preamble_len %hhu", 
        ((uint8_t*)retval)[0], ((uint8_t*)retval)[1], (uint16_t)(*retval * 10), hb_time, preamble_len);
    
    return hb_time;
    
}

void send_heartbeat_message(robusto_peer_t *peer, e_media_type media_type)
{
    uint64_t curr_time = r_millis();
    robusto_media_t *info = get_media_info(peer, media_type);
    uint8_t *hb_msg;
    uint16_t deka_ms_diff;

    if (info->postpone_qos) {
        ROB_LOGW(heartbeat_log_prefix, "Postponing heartbeat to %s using %s", peer->name, media_type_to_str(media_type));
        return;
    }
    if (((info->last_send < curr_time - HEARTBEAT_PROBLEM_MARGIN_MS) && (info->problem != media_problem_none) ) || // Either we have a problem
        ((info->last_send < curr_time - HEARTBEAT_IDLE_MARGIN_MS)  && (info->problem == media_problem_none) )|| // or we are idle
        (info->last_receive < curr_time - (HEARTBEAT_IDLE_MARGIN_MS * 2)) || // or we haven't heard from the peer
        (info->last_peer_receive < curr_time - (HEARTBEAT_IDLE_MARGIN_MS * 2)) || // or they haven't heard from us
        (info->last_sent_heartbeat < curr_time - (HEARTBEAT_IDLE_MARGIN_MS * 3)) // or we haven't sent a heart beat
    )
    {
        if (info->problem == media_problem_none) {
            ROB_LOGD(heartbeat_log_prefix, "Sending heartbeat to %s, mt %hhu", peer->name, (uint8_t)media_type);
        } else {
            ROB_LOGW(heartbeat_log_prefix, "Sending heartbeat to problematic peer %s using %s", peer->name, media_type_to_str(media_type));
        }
        
        deka_ms_diff = calc_deka_ms_since(info->last_receive, curr_time);
        int hb_msg_len = robusto_make_multi_message_internal(MSG_HEARTBEAT, 0, 0, NULL, 0, (uint8_t *)&deka_ms_diff, 2, &hb_msg);
        // Heartbeats are one-way and doesn't wait for receipts
        rob_ret_val_t queue_ret_val = send_message_raw_internal(peer, media_type, hb_msg, hb_msg_len, NULL, 
            false, (info->state == media_state_recovering) ? media_qit_recovery : media_qit_heartbeat, 0, robusto_mt_none, false);
        if (queue_ret_val != ROB_OK && queue_ret_val != ROB_ERR_QUEUE_FULL) {
            // If we get a problem here that is not that the queue is full, there might be a more serious internal issue, it is immidiately considered a problem.
            ROB_LOGE(heartbeat_log_prefix, "Early error sending heartbeat to %s, mt %hhu, res %hi, ", peer->name, (uint8_t)media_type, queue_ret_val);
            set_state(peer, info, media_type, media_state_problem, media_problem_technical);
        }
    }
}

void peer_heartbeat(robusto_peer_t *peer)
{
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    /* This client will likely go to sleep (matters for QoS) */
    if (peer->sleeper) {
        ROB_LOGI(heartbeat_log_prefix, "Skipping heartbeats to sleeper peer %s.", peer->name);
        return;
    }
#endif

    uint64_t last_heartbeat = 0;
    uint64_t curr_time = r_millis();
    if (curr_time - HEARTBEAT_IDLE_MARGIN_MS > 0) {
        // Some platforms are so fast (like Native), that we might get here before HEARTBEAT_IDLE_MARGIN_MS has passed
        uint64_t last_heartbeat = curr_time - HEARTBEAT_IDLE_MARGIN_MS;
    }
    
    for (uint16_t media_type = 1; media_type < 256; media_type = media_type * 2)
    {
        if ((peer->supported_media_types & media_type) != media_type)
        {
            continue;
        }
#ifdef CONFIG_ROBUSTO_SUPPORTS_TTL
        if (media_type == robusto_mt_ttl)
        {
            // Not Implemented
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
        if (media_type == robusto_mt_ble)
        {
            send_heartbeat_message(peer, robusto_mt_ble);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
        if (media_type == robusto_mt_espnow)
        {
            send_heartbeat_message(peer, robusto_mt_espnow);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
        if (media_type == robusto_mt_lora)
        {
            send_heartbeat_message(peer, robusto_mt_lora);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        if (media_type == robusto_mt_i2c)
        {
            send_heartbeat_message(peer, robusto_mt_i2c);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
        if (media_type == robusto_mt_canbus)
        {
            send_heartbeat_message(peer, robusto_mt_canbus);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_UMTS
        if (media_type == robusto_mt_umts)
        {
            // Not Implemented
        }
#endif
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
        if (media_type == robusto_mt_mock)
        {
            send_heartbeat_message(peer, robusto_mt_mock);
        }
#endif
    }
}

void heartbeat_cb()
{
    
    struct robusto_peer *peer;
    ROB_LOGD("heartbeat_log_prefix", "in heartbeats()");
 
    SLIST_FOREACH(peer, get_peer_list(), next)
    {
        // Do not disturb the presentation
        if (peer->state != PEER_PRESENTING) {
            peer_heartbeat(peer);
        }
        
    }
    
}
void heartbeat_shutdown_cb()
{
}

void init_qos_heartbeat(char *_log_prefix)
{
    heartbeat_log_prefix = _log_prefix;
    robusto_register_recurrence(&heartbeat);
}