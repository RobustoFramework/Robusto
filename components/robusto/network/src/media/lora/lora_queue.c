
/**
 * @file lora_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Lora worker maintain and monitor the Lora work queue and uses callbacks to notify the user code
 * This code uses robusto_work_queue to automatically handle queues, semaphores and tasks
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

#include "lora_queue.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA


#include <robusto_sys_queue.h>

#include <robusto_logging.h>
#include <string.h>

// The queue context
struct queue_context lora_queue_context;

static char *lora_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(lora_work_q, media_queue_item) 
lora_work_q;

media_queue_item_t *lora_first_queueitem() 
{
    return STAILQ_FIRST(&lora_work_q); 
}

void lora_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&lora_work_q, items);
}
void lora_insert_tail(queue_context_t * q_context, media_queue_item_t *new_item) { 
    STAILQ_INSERT_TAIL(&lora_work_q, new_item, items);
}

queue_context_t *lora_get_queue_context() {
    return &lora_queue_context;
}

void lora_cleanup_queue_task(media_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {    
        free(queue_item->peer);
        free(queue_item->data);
        free(queue_item);
    }
    cleanup_queue_task(&lora_queue_context);
}

void lora_set_queue_blocked(bool blocked) {
    set_queue_blocked(&lora_queue_context,blocked);
}

void lora_shutdown_worker() {
    ROB_LOGI(lora_worker_log_prefix, "Telling LoRa worker to shut down.");
    lora_queue_context.shutdown = true;
}

rob_ret_val_t lora_init_worker(work_callback work_cb, poll_callback poll_cb, char *_log_prefix)
{
    lora_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&lora_work_q);

    lora_queue_context.first_queue_item_cb = &lora_first_queueitem; 
    lora_queue_context.remove_first_queueitem_cb = &lora_remove_first_queue_item; 
    lora_queue_context.insert_tail_cb = &lora_insert_tail;
    lora_queue_context.insert_head_cb = NULL;
    lora_queue_context.on_work_cb = work_cb; 
    lora_queue_context.on_poll_cb = poll_cb;
    lora_queue_context.max_task_count = 1;
    lora_queue_context.multitasking = false;
    // This queue cannot start processing items until lora is initialized
    lora_queue_context.blocked = true;
  /* If set, worker will shut down */
    lora_queue_context.watchdog_timeout = CONFIG_ROB_RECEIPT_TIMEOUT_MS;
    

    return init_work_queue(&lora_queue_context, _log_prefix, "LoRa Queue");      
}
#endif
