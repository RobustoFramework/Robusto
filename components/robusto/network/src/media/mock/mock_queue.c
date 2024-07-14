/**
 * @file mock_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Mock queue and worker implementation
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

#include "mock_queue.h"
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

#ifdef USE_ARDUINO
#include "compat/arduino_sys_queue.h"
#else
#include <robusto_sys_queue.h>
#include <stddef.h>
#include <stdlib.h>
#endif

#include <robusto_logging.h>

// The queue context
queue_context_t *mock_queue_context;

static char *mock_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(mock_work_q, media_queue_item) 
mock_work_q;

void *mock_first_queueitem() 
{
    return STAILQ_FIRST(&mock_work_q); 
}

void mock_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&mock_work_q, items);
}

void mock_insert_tail(queue_context_t * q_context, media_queue_item_t *new_item) { 
    STAILQ_INSERT_TAIL(&mock_work_q, new_item, items);
}


void mock_cleanup_queue_task(media_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {    

        free(queue_item);
    }
    cleanup_queue_task(mock_queue_context);
}

queue_context_t *mock_get_queue_context() {
    return mock_queue_context;
}

void mock_set_queue_blocked(bool blocked) {
    set_queue_blocked(mock_queue_context,blocked);
}

void mock_shutdown_worker() {
    ROB_LOGI(mock_worker_log_prefix, "Telling the mock worker to shut down.");
    mock_queue_context->shutdown = true;
}

rob_ret_val_t mock_init_worker(work_callback work_cb, poll_callback poll_cb, char *_log_prefix)
{
    mock_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&mock_work_q);
    mock_queue_context = malloc(sizeof(queue_context_t));

    mock_queue_context->first_queue_item_cb = mock_first_queueitem; 
    mock_queue_context->remove_first_queueitem_cb = mock_remove_first_queue_item; 
    mock_queue_context->insert_tail_cb = mock_insert_tail;
    mock_queue_context.insert_head_cb = NULL;
    mock_queue_context->on_work_cb = work_cb; 
    mock_queue_context->on_poll_cb = poll_cb;
    mock_queue_context->max_task_count = 1;
    // This queue cannot start processing items until we want the test to start
    mock_queue_context->blocked = true;
    mock_queue_context->multitasking = false;
    mock_queue_context->watchdog_timeout = 7;
    mock_queue_context->shutdown = false;
    mock_queue_context->log_prefix = _log_prefix;

    return init_work_queue(mock_queue_context, _log_prefix, "Mock queue");      
}
#endif
