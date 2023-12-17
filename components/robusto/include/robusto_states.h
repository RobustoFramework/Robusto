/**
 * @file robusto_states.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief States.
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
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

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief These are some useful state codes that is used in Robusto.
 */

typedef enum e_rob_state
{
    ROB_ST_NOT_RUNNING = 0,
    ROB_ST_RUNNING     = 1,
    ROB_ST_PAUSED      = 2,
    ROB_ST_RETRYING    = 3,
    // End states
    ROB_ST_DONE        = 4,  // Done, but we don't know if we've failed or not
    ROB_ST_SUCCEEDED   = 5,
    ROB_ST_FAILED      = 6,
    ROB_ST_TIMED_OUT   = 7,
    ROB_ST_ABORTED     = 8

} e_rob_state_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
