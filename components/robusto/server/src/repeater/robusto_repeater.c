/**
 * @file robusto_repeater.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Base repeatomg functionality 
 * @version 0.1
 * @date 2023-07-28
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
 * SUBSTITUTE GOODS OR recurrenceS; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robusto_repeater.h>
#include <robusto_server_init.h>
#include <robusto_concurrency.h>
#include <robusto_repeater.h>
#include <robusto_logging.h>

#include <stdio.h>
#include <stdbool.h>

// TODO: How to handle structures like the recurrences array? This is a general question, see other todo on runlevels
char *repeater_log_prefix = "Before initiation";

bool recurrence_shutdown = false;

uint8_t recurrence_count = 0;

// TODO: Use a linked list here instead, perhaps?
recurrence_t *recurrences[CONFIG_ROBUSTO_MAX_RECURRENCES];

rob_ret_val_t robusto_register_recurrence(recurrence_t * recurrence) {
    if (recurrence->recurrence_callback == NULL) {

        ROB_LOGE(repeater_log_prefix, "Failed registering recurrence with repeater, no recurrence callback assigned.");
        return ROB_FAIL;
    }
    recurrences[recurrence_count] = recurrence;
    recurrence_count++;
    ROB_LOGI(repeater_log_prefix, "Repeater: Recurrence '%s' successfully added.", recurrence->recurrence_name);
    return ROB_OK;
}


void run_all_repeaters_now() {
    recurrence_t * curr_recurrence = NULL;
    for (uint16_t i = 0; i < recurrence_count; i++) {
        curr_recurrence = recurrences[i];
        curr_recurrence->recurrence_callback();
    }
}



void run_repeaters() {
    recurrence_t * curr_recurrence = NULL;
    for (uint16_t i = 0; i < recurrence_count; i++) {
        curr_recurrence = recurrences[i];
        if (curr_recurrence->skips_left == 0) {
            ROB_LOGD(repeater_log_prefix, "Repeating: %s", curr_recurrence->recurrence_name);
            robusto_yield();
            curr_recurrence->recurrence_callback();
            ROB_LOGD(repeater_log_prefix, "Done %s", curr_recurrence->recurrence_name);
            curr_recurrence->skips_left = curr_recurrence->skip_count;
        } else {
            curr_recurrence->skips_left--;
        }
    }
}

/**
 * @brief The repeater tasks periodically takes sampes of the current state
 * It uses that history data to perform some simple statistical calculations,
 * helping out with finding memory leaks.
 * 
 * 
 * @param arg 
 */

void repeater_task(void *arg)
{  
    ROB_LOGI(repeater_log_prefix, "Repeater task running..");
    while (!recurrence_shutdown) {
        // Call different repeaters
        run_repeaters();

        r_delay(CONFIG_ROBUSTO_REPEATER_DELAY_MS);
        // TODO: Add problem and warning callbacks to make it possible to raise the alarm if there are any issues.

    } 
    robusto_delete_current_task();

}

void robusto_repeater_stop() {
     ROB_LOGI(repeater_log_prefix, "Telling repeater to shut down.");
     recurrence_shutdown = true;
}

void robusto_repeater_start() {
    rob_task_handle_t *handle;
    char task_name[30] = "Repeater task";
    robusto_create_task_custom(repeater_task, NULL, &task_name, &handle, 1, 16384);
    ROB_LOGI(repeater_log_prefix, "Repeater initiated.");
}

void robusto_repeater_init(char * _log_prefix) {

    repeater_log_prefix = _log_prefix;
    // Register the internal repeaters

}