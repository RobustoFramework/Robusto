
/**
 * @file robusto_orchestration.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Robusto orchestration initialization
 * @todo Re-implement old orchestrator in Robusto (before winter)
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

#include <robusto_time.h>

/* How long will we be running if no one extends our session */
#define ROBUSTO_AWAKE_TIME_uS 40 * SECOND
/* The most amount of time the peer gives itself until it goes to sleep */
#define ROBUSTO_AWAKE_TIMEBOX_uS ROBUSTO_AWAKE_TIME_uS * 2

/* How long will each cycle be*/
#define ROBUSTO_SLEEP_TIME_uS HOUR - ROBUSTO_AWAKE_TIME_uS

#if ROBUSTO_AWAKE_TIMEBOX_uS - ROBUSTO_SLEEP_TIME_uS > ROBUSTO_SLEEP_TIME_uS
#error "ROBUSTO_AWAKE_TIMEBOX - ROBUSTO_SLEEP_TIME_uS  cannot be longer than the ROBUSTO_SLEEP_TIME_uS"
#endif
/* Will we wait a little extra to avoid flooding? */
#define ROBUSTO_AWAKE_MARGIN_uS 2000000

/* How long should we sleep until next retry (in us) */
#define ROBUSTO_ORCHESTRATION_RETRY_WAIT_uS 30000000


/* Callbacks that are called before sleeping, return true to stop going to sleep. */
typedef bool(before_sleep)();