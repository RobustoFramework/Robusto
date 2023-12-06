/**
 * @file mock_recover.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Mock connection recovery implementation. 
 * @todo: Implement this for real where applicable
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

#include "mock_recover.h"

#include <robusto_media.h>
#include <robusto_peer.h>
#include <robusto_time.h>
#include <robusto_message.h>
#include <robusto_logging.h>
#include <robusto_concurrency.h>
#include <robusto_qos.h>
static char *mock_recovery_log_prefix;

void mock_recover(recover_params_t * params)
{
    ROB_LOGI(mock_recovery_log_prefix, "In mock_recover()");
    
    /*
    params->info->state = media_state_recovering;
    ROB_LOGI(mock_recovery_log_prefix, "media_state_recovering set");
    r_delay(50);
    params->info->state = media_state_down;
    ROB_LOGI(mock_recovery_log_prefix, "media_state_down set.");
    r_delay(50);
    params->info->state = media_state_problem;
    ROB_LOGI(mock_recovery_log_prefix, "media_state_problem set.");    
    r_delay(50);
    params->info->state = media_state_working;
    ROB_LOGI(mock_recovery_log_prefix, "media_state_working set, exiting.");    
    */
    robusto_delete_current_task();

}

void init_mock_recover(char *_log_prefix)
{
    mock_recovery_log_prefix = _log_prefix;
}