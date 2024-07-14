/**
 * @file espnow_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The ESP-NOW queue maintain and monitor the ESP-NOW work queue and uses callbacks to notify the user code
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
#include "espnow_queue.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW


#include <robusto_sys_queue.h>

#include <robusto_logging.h>
#include <string.h>

// The queue context
queue_context_t espnow_queue_context;

static char *espnow_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(espnow_work_q, media_queue_item ) 
espnow_work_q;

media_queue_item_t *espnow_first_queueitem() 
{
    return STAILQ_FIRST(&espnow_work_q); 
}

void espnow_remove_first_queue_item(queue_context_t * q_context){
    STAILQ_REMOVE_HEAD(&espnow_work_q, items);
    q_context->count--;
}
void espnow_insert_tail(queue_context_t * q_context, media_queue_item_t *new_item) { 
    STAILQ_INSERT_TAIL(&espnow_work_q, new_item, items);
    q_context->count++;
}

void espnow_insert_head(queue_context_t * q_context, media_queue_item_t *new_item) { 
    STAILQ_INSERT_HEAD(&espnow_work_q, new_item, items);
    q_context->count++;
}

queue_context_t *espnow_get_queue_context() {
    return &espnow_queue_context;
}

void espnow_cleanup_queue_task(media_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {    
        free(queue_item->peer);
        free(queue_item->data);
        free(queue_item);
    }
    cleanup_queue_task(&espnow_queue_context);
}

void espnow_set_queue_blocked(bool blocked) {
    set_queue_blocked(&espnow_queue_context,blocked);
}

void espnow_shutdown_worker() {
    ROB_LOGI(espnow_worker_log_prefix, "Telling LoRa worker to shut down.");
    espnow_queue_context.shutdown = true;
}

rob_ret_val_t espnow_init_worker(work_callback work_cb, poll_callback poll_cb, char *_log_prefix)
{
    espnow_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&espnow_work_q);

    espnow_queue_context.first_queue_item_cb = &espnow_first_queueitem; 
    espnow_queue_context.remove_first_queueitem_cb = &espnow_remove_first_queue_item; 
    espnow_queue_context.insert_tail_cb = &espnow_insert_tail;
    espnow_queue_context.insert_head_cb = &espnow_insert_head;
    espnow_queue_context.on_work_cb = work_cb; 
    espnow_queue_context.on_poll_cb = poll_cb;
    espnow_queue_context.max_task_count = 1;
    espnow_queue_context.normal_max_count = 3;
    espnow_queue_context.important_max_count = 5;
    espnow_queue_context.multitasking = false;
    // This queue cannot start processing items until espnow is initialized
    espnow_queue_context.blocked = true;
  /* If set, worker will shut down */
    espnow_queue_context.watchdog_timeout = CONFIG_ROB_RECEIPT_TIMEOUT_MS;
    

    return init_work_queue(&espnow_queue_context, _log_prefix, "ESP-NOW Queue");      
}
#endif
