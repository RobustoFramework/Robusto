

/**
 * @file qos_scoring.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief QoS scoring implementation
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


#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <robusto_logging.h>
#include "robusto_qos.h"
#include "robusto_qos_init.h"
#include <robusto_repeater.h>
#include <robusto_media.h>
#include <robusto_retval.h>
#include <robusto_peer.h>


#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "../media/i2c/i2c_messaging.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "../media/lora/lora_messaging.hpp"
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
#include "../media/espnow/espnow_messaging.h"
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
#include "../media/canbus/canbus_messaging.h"
#endif

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "../media/mock/mock_peer.h"
#endif



static char *scoring_log_prefix;

void scoring_cb();
void scoring_shutdown_cb();

static char scorings_name[19] = "scorings\x00";

recurrence_t scoring = {
    recurrence_name : &scorings_name,
    skip_count : CONFIG_ROBUSTO_PEER_HEARTBEAT_SKIP_COUNT, // TODO: Should scoring be done more often?
    skips_left : 0,
    recurrence_callback : &scoring_cb,
    shutdown_callback : &scoring_shutdown_cb
};

void add_to_history(robusto_media_t *stats, bool sending, rob_ret_val_t result)
{
    if (sending)
    {
        if (result == ROB_OK)
        {
            stats->send_successes++;
            stats->last_send = r_millis();
        }
        else
        {
            stats->send_failures++;
        }
    }
    else
    {
        if (result == ROB_OK)
        {
            stats->receive_successes++;
            stats->last_receive = r_millis();
        }
        else
        {
            stats->receive_failures++;
        }
    }
}

/**
 * @brief Calculate the suitability
 *
 * @param data_length The length of data to transfer
 * @param base_value The initial position, is this generally a good or worse media
 * @param base_offset 
 * @param multiplier
 * @return float
 */

uint32_t robusto_calc_suitability(e_media_type media_type, uint32_t payloadSize) {
    int phc;
    switch (media_type) {
        #if CONFIG_ROBUSTO_SUPPORTS_CANBUS
        case robusto_mt_canbus:
        // CAN bus is great with small volumes, but quickly start losing out.
            phc = (payloadSize - CANBUS_MESSAGE_OFFSET <= 8) ? 100 : 80 - ((payloadSize - CANBUS_MESSAGE_OFFSET - 8) / 10);
        // TODO: Shoud stop showing the data length with a lot of data that won't be used in many cases, especially like the 20 bytes here
            break;
        #endif
        case robusto_mt_espnow:
            phc = 100; // ESP-NOW is quite flexible
            break;
        case robusto_mt_ble:
            phc = 90; // BLE is also quite flexible, but has smaller MTUs and a bit slower
            break;
        case robusto_mt_i2c:
            phc = 90; // Assuming I2C is generally good
            break;
        case robusto_mt_lora:
        // TODO: We should create a kind of CANBUS_MESSAGE_OFFSET for LoRa and use here and in code.
            phc = 80 - (payloadSize - ROBUSTO_CRC_LENGTH / 10); // Decrease with size
            break;
        default:
            ROB_LOGE(scoring_log_prefix, "robusto_calc_suitability: Unsupported media type: %s", media_type_to_str(media_type));
            phc = 50; // Default case
    }
    if (phc > 100) phc = 100; // Ensure not to exceed 100
    if (phc < 0) phc = 0; // Ensure not to drop below 0
    return phc;
}
/**
 * @brief Add the latest failure rate to history, calculate averages
 * 
 * @param stats 
 * @param rate 
 * @return float 
 */
float add_to_failure_rate_history(robusto_media_t *stats, float rate)
{
    // Calc average of the current history + rate, start with summarizing
    float sum = 0;
    for (int i = 0; i < FAILURE_RATE_HISTORY_LENGTH; i++)
    {
        sum += stats->failure_rate_history[i];
        // ROB_LOGD(peer_log_prefix, "FRH history %i: fr: %f", i, stats->failure_rate_history[i]);
    }
    sum += rate;
    // Move the array one step to the left
    memmove((void *)(stats->failure_rate_history), (void *)(stats->failure_rate_history) + sizeof(float), sizeof(float) * (FAILURE_RATE_HISTORY_LENGTH - 1));
    stats->failure_rate_history[FAILURE_RATE_HISTORY_LENGTH - 1] = rate;

    ROB_LOGD(scoring_log_prefix, "FRH avg %f", (float)(sum / (FAILURE_RATE_HISTORY_LENGTH + 1)));

    return sum / (float)(FAILURE_RATE_HISTORY_LENGTH + 1);
}

void update_score(robusto_peer_t *peer, e_media_type media_type)
{
    // TODO: Clarify in documentation that we only care about our ability to reach others,
    // not their ability to reach us. This is for us to be able to choose the best medium to
    // send a message to them.
    // They might make a different choice when messaging us based on our responses, which is how we inform them.
    // And we cannot inform them if the do not send anything.

    uint64_t curr_time = r_millis();
    robusto_media_t *curr_info = get_media_info(peer, media_type);
    // If we have no sample data, that is a small negative.
    float failure_rate = 0.1;

    if (curr_info->send_successes > 0)
    {
        ROB_LOGD(scoring_log_prefix, "Scored peer %lu, %lu", curr_info->send_failures, curr_info->send_successes);
        failure_rate = (float)((float)curr_info->send_failures / (float)curr_info->send_successes);
    }
    else if (curr_info->send_failures > 0)
    {
        // If there are only failures, that causes a 1 as a failure fraction
        failure_rate = 1;
    }

    curr_info->failure_rate = add_to_failure_rate_history(curr_info, failure_rate);
    ROB_LOGD(scoring_log_prefix, "Scored peer %s, media %hhu, avg failure rate %f", peer->name, media_type, curr_info->failure_rate );
#define FAILURE_COUNT 4
    // We do not want large numbers at all, as this makes new change not matter
    if ((curr_info->send_successes + curr_info->send_failures) > 5)
    {
        ROB_LOGI(scoring_log_prefix, "Resettings stats");
        float failure_quotient = 0;
        if (curr_info->send_successes > 0)
        {
            failure_quotient = (float)curr_info->send_failures / (float)curr_info->send_successes;
        }
        if (failure_quotient == 1)
        {
            curr_info->send_successes = 0;
            curr_info->send_failures = 0;
        }
        else if (failure_quotient > FAILURE_COUNT)
        {
            curr_info->send_successes = 0;
            curr_info->send_failures = FAILURE_COUNT;
        }
        else if (failure_quotient < (1 / FAILURE_COUNT))
        {
            curr_info->send_successes = FAILURE_COUNT;
            curr_info->send_failures = 0;
        }
        else
        {
            curr_info->send_successes = 1 / failure_quotient;
            curr_info->send_failures = 1 * failure_quotient;
        }
    }

}

void peer_scoring(robusto_peer_t *peer)
{

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
            // Not Implemented
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
        if (media_type == robusto_mt_espnow)
        {
            update_score(peer, robusto_mt_espnow);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
        if (media_type == robusto_mt_lora)
        {
            update_score(peer, robusto_mt_lora);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        if (media_type == robusto_mt_i2c)
        {
            update_score(peer, robusto_mt_i2c);
        }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
        if (media_type == robusto_mt_canbus)
        {
            update_score(peer, robusto_mt_espnow);
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

            update_score(peer, robusto_mt_mock);
        }
#endif
    }
}

void scoring_cb()
{
    struct robusto_peer *peer;
    SLIST_FOREACH(peer, get_peer_list(), next)
    {
        peer_scoring(peer);
    }
}

void scoring_shutdown_cb()
{
}


void init_qos_scoring(char *_log_prefix)
{
    scoring_log_prefix = _log_prefix;
    robusto_register_recurrence(&scoring);
}