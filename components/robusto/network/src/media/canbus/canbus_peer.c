/**
 * @file canbus_peer.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief I2C peer implementation
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

#include "canbus_peer.h"
#if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS)

#include <robusto_logging.h>
#include <robusto_time.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <string.h>
#include <inttypes.h>

static char * canbus_peer_log_prefix;



void canbus_peer_init_peer(robusto_peer_t *peer)
{
    memset(peer->canbus_info.failure_rate_history, 0, sizeof(float) * FAILURE_RATE_HISTORY_LENGTH);
    peer_stat_reset(&peer->canbus_info);
    set_state(peer, &peer->canbus_info, robusto_mt_canbus, media_state_initiating, media_problem_none);
    
}




void canbus_peer_init(char * _log_prefix) {
    canbus_peer_log_prefix = _log_prefix;

};
#endif