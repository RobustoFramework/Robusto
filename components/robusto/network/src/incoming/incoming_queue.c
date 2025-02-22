
/**
 * @file incoming_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The queue for incoming messages
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
#include "robusto_incoming.h"
#include <robusto_message.h>
#ifdef USE_ARDUINO
#include "compat/arduino_sys_queue.h"
#else
#include <robusto_sys_queue.h>
#include <stddef.h>
#include <stdlib.h>
#endif

#include <robusto_logging.h>

// The queue context
queue_context_t *incoming_queue_context;

static char *incoming_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(incoming_work_q, incoming_queue_item) 
incoming_work_q;

void *incoming_first_queueitem() 
{
    return STAILQ_FIRST(&incoming_work_q); 
}

void incoming_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&incoming_work_q, items);
}

void incoming_insert_tail(queue_context_t * q_context, void *new_item) { 
    STAILQ_INSERT_TAIL(&incoming_work_q, (incoming_queue_item_t *)new_item, items);
}


rob_ret_val_t incoming_safe_add_work_queue(incoming_queue_item_t * queue_item) { 
    return safe_add_work_queue(incoming_queue_context, queue_item, false);
}

void incoming_cleanup_queue_task(incoming_queue_item_t *queue_item) {

    cleanup_queue_task(incoming_queue_context);
}

queue_context_t *incoming_get_queue_context() {
    return incoming_queue_context;
}

void incoming_set_queue_blocked(bool blocked) {
    set_queue_blocked(incoming_queue_context,blocked);
}



void incoming_shutdown_worker() {
    ROB_LOGI(incoming_worker_log_prefix, "Telling incoming worker to shut down.");
    incoming_queue_context->shutdown = true;
}

rob_ret_val_t incoming_init_worker(incoming_callback_cb work_cb, char *_log_prefix)
{
    incoming_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&incoming_work_q);
    incoming_queue_context = malloc(sizeof(queue_context_t));

    incoming_queue_context->first_queue_item_cb = incoming_first_queueitem; 
    incoming_queue_context->remove_first_queueitem_cb = incoming_remove_first_queue_item; 
    incoming_queue_context->insert_tail_cb = incoming_insert_tail;
    incoming_queue_context->insert_head_cb = NULL;
    incoming_queue_context->on_work_cb = (work_callback *) work_cb; 
    incoming_queue_context->on_poll_cb = NULL;
    incoming_queue_context->max_task_count = 1;
    incoming_queue_context->max_task_count = 1;
    incoming_queue_context->normal_max_count = 4;
    incoming_queue_context->important_max_count = 7;
    // This queue cannot start processing items until we want the test to start
    incoming_queue_context->blocked = false;
    incoming_queue_context->multitasking = false;
    incoming_queue_context->watchdog_timeout = 7;
    incoming_queue_context->shutdown = false;
    incoming_queue_context->log_prefix = _log_prefix;

    return init_work_queue(incoming_queue_context, _log_prefix, "Incoming queue");      
}

