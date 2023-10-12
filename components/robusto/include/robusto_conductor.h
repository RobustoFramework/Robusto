
/**
 * @file robusto_conductor.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
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
#include <robconfig.h>
#include <robusto_time.h>
#include <inttypes.h>

/* The most amount of time the peer gives itself until it goes to sleep */
#define ROBUSTO_AWAKE_TIMEBOX_MS CONFIG_ROBUSTO_AWAKE_TIME_MS * 2

#define ROBUSTO_CONDUCTOR_CLIENT_ID 1
#define ROBUSTO_CONDUCTOR_SERVER_ID 2

#if ROBUSTO_AWAKE_TIMEBOX_MS - ROBUSTO_SLEEP_TIME_MS > ROBUSTO_SLEEP_TIME_MS
#error "ROBUSTO_AWAKE_TIMEBOX - ROBUSTO_SLEEP_TIME_MS  cannot be longer than the ROBUSTO_SLEEP_TIME_MS"
#endif

/* Callbacks that are called before sleeping, return true to stop going to sleep. */
typedef bool (before_sleep)(void);


/* Optional callback that happen before the system is going to sleep */
void set_before_sleep(before_sleep * on_before_sleep_cb);

void update_next_availability_window();

bool ask_for_time(uint32_t ask);

void take_control();
void give_control(sdp_peer * peer); 

void robusto_conductor_init(char * _log_prefix, before_sleep _on_before_sleep_cb); 

void sleep_until_peer_available(sdp_peer *peer, uint32_t margin_us);

//  Availability When/Next messaging

int robusto_conductor_send_when_message(sdp_peer *peer);

int robusto_conductor_send_next_message(work_queue_item_t *queue_item);
void robusto_conductor_parse_next_message(work_queue_item_t *queue_item);