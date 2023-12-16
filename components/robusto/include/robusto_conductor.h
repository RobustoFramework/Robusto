
/**
 * @file robusto_conductor.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto conductor definitions
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
#include <robusto_time.h>
#include <robusto_peer_def.h>
#include <inttypes.h>
#ifdef __cplusplus
extern "C"
{
#endif

/* The most amount of time the peer gives itself until it goes to sleep */
#define ROBUSTO_AWAKE_TIMEBOX_MS CONFIG_ROBUSTO_CONDUCTOR_SERVER_AWAKE_TIME_MS * 2

#if CONFIG_ROBUSTO_CONDUCTOR_SERVER_CYCLE_TIME_MS < ROBUSTO_AWAKE_TIMEBOX_MS
#error "You cannot set the conductor cycle time to less than twice the awake time"
#endif

// TODO: Work through all Robusto Client ids
#define ROBUSTO_CONDUCTOR_CLIENT_SERVICE_ID 1980U
#define ROBUSTO_CONDUCTOR_SERVER_SERVICE_ID 1981U


#define ROBUSTO_CONDUCTOR_MSG_WHEN 1U
#define ROBUSTO_CONDUCTOR_MSG_THEN 2U
#define ROBUSTO_CONDUCTOR_MSG_MORE 3U



/* Callbacks that are called before sleeping, return true to prohibit sleep. */
typedef bool(before_sleep)(void);

/* Server functionality*/


/**
 * @brief Ask to wait with sleep for a specific amount of time from now
 * @param ask Returns false if request is denied
 */
bool robusto_conductor_server_ask_for_time(uint32_t ask);

/**
 * @brief The conductor takes control and unless asked for more time, falls asleep
 * until next availability window
 */
void robusto_conductor_server_take_control();

/**
 * @brief Send information about the next window to a client peer
 *
 * @param message
 * @return int
 */
int robusto_conductor_server_send_then_message(robusto_peer_t *peer);


/**
 * @brief Time since the conductor started conducting
 * 
 * @return uint32_t Time in milliseconds
 */

uint32_t robusto_conductor_server_get_time_since_start();

/* Client functionality*/


/**
 * @brief Check with the peer when its available next, and goes to sleep until then.
 * @param peer The peer that one wants to follow
 */
void robusto_conductor_client_give_control(robusto_peer_t *peer);

/**
 * @brief Send a "When"-message that asks the peer to describe themselves
 *
 * @return int A handle to the created conversation
 */
int robusto_conductor_client_send_when_message(robusto_peer_t *peer);
/**
 * @brief Register a callback that is called before sleep
 * 
 * @param _on_before_sleep_cb The callback
 */
void robusto_conductor_server_set_before_sleep(before_sleep *_on_before_sleep_cb);


#ifdef __cplusplus
} /* extern "C" */
#endif