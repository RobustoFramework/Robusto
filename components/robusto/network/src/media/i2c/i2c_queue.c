/**
 * @file i2c_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The i2c worker maintain and monitor the i2c work queue and uses callbacks to notify the user code
 * This code uses sdp_work_queue to automatically handle queues, semaphores and tasks
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
#include "i2c_queue.h"
#if defined(CONFIG_ROBUSTO_SUPPORTS_I2C) 

#include <robusto_sys_queue.h>

#include <robusto_logging.h>
#include <robusto_queue.h>
#include <string.h>
#include <robusto_system.h>
// The queue context
struct queue_context i2c_queue_context;

static char *i2c_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(i2c_work_q, media_queue_item) 
i2c_work_q;

media_queue_item_t *i2c_first_queueitem() 
{
    return STAILQ_FIRST(&i2c_work_q); 
}

void i2c_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&i2c_work_q, items);
}
void i2c_insert_tail(queue_context_t * q_context, media_queue_item_t *new_item) { 
    STAILQ_INSERT_TAIL(&i2c_work_q, new_item, items);
}

void i2c_cleanup_queue_task(media_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {    
        robusto_free(queue_item->data);
        robusto_free(queue_item);
    }
    cleanup_queue_task(&i2c_queue_context);
}

queue_context_t *i2c_get_queue_context() {
    return &i2c_queue_context;
}

void i2c_set_queue_blocked(bool blocked) {
    set_queue_blocked(&i2c_queue_context,blocked);
}

void i2c_shutdown_worker() {
    ROB_LOGI(i2c_worker_log_prefix, "Telling i2c worker to shut down.");
    i2c_queue_context.shutdown = true;
}

rob_ret_val_t i2c_init_worker(work_callback work_cb, poll_callback poll_cb, char *_log_prefix)
{
    i2c_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&i2c_work_q);

    i2c_queue_context.first_queue_item_cb = i2c_first_queueitem; 
    i2c_queue_context.remove_first_queueitem_cb = &i2c_remove_first_queue_item; 
    i2c_queue_context.insert_tail_cb = &i2c_insert_tail;
    i2c_queue_context.insert_head_cb = NULL;
    i2c_queue_context.on_work_cb = work_cb; 
    i2c_queue_context.on_poll_cb = poll_cb;
    i2c_queue_context.max_task_count = 1;
    // This queue cannot start processing items until i2c is initialized
    i2c_queue_context.blocked = true;
    i2c_queue_context.multitasking = false;
    i2c_queue_context.watchdog_timeout = 5000;

    return init_work_queue(&i2c_queue_context, _log_prefix, "i2c Queue");      
}

#endif