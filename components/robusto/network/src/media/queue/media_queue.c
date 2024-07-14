/**
 * @file media_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief A generalized media queue implementations, used by the different medias
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
#include "media_queue.h"
#include "robusto_media_def.h"
#include "robusto_system.h"

#include <robusto_sys_queue.h>

#include <robusto_logging.h>
#include <string.h>

// The queue type definition
typedef STAILQ_HEAD(new_work_q_s, media_queue_item) new_media_q_t;

/* Expands to a declaration for the work queue */

media_queue_item_t *get_first_queueitem(queue_context_t *q_context)
{
    return STAILQ_FIRST((new_media_q_t *)(q_context->work_queue));
}

void remove_first_queue_item(queue_context_t *q_context)
{
    STAILQ_REMOVE_HEAD((new_media_q_t *)(q_context->work_queue), items);
}
void insert_at_tail(queue_context_t *q_context, media_queue_item_t *new_item)
{
    STAILQ_INSERT_TAIL((new_media_q_t *)(q_context->work_queue), new_item, items);
}

void media_cleanup_queue_task(queue_context_t *q_context, media_queue_item_t *queue_item)
{
    if (queue_item != NULL)
    {
        free(queue_item->peer);
        free(queue_item->data);
        free(queue_item);
    }
    cleanup_queue_task(q_context);
}

queue_context_t *create_media_queue(char *_log_prefix, char *queue_name, 
    work_callback work_cb, poll_callback poll_cb)
{

    queue_context_t *new_queue_context = robusto_malloc(sizeof(queue_context_t));

    new_queue_context->work_queue = robusto_malloc(sizeof(new_media_q_t));
    // Initialize the work queue
    STAILQ_INIT((new_media_q_t *)(new_queue_context->work_queue));

    new_queue_context->first_queue_item_cb = &get_first_queueitem;
    new_queue_context->remove_first_queueitem_cb = &remove_first_queue_item;
    new_queue_context->insert_tail_cb = &insert_at_tail;
    new_queue_context->insert_head_cb = NULL;
    new_queue_context->on_work_cb = work_cb;
    new_queue_context->on_poll_cb = poll_cb;
    new_queue_context->max_task_count = 1;
    new_queue_context->multitasking = false;
    // This queue cannot start processing items until the media is initialized
    new_queue_context->blocked = true;
    new_queue_context->shutdown = false;
    new_queue_context->log_prefix = _log_prefix;
    /* If set, worker will shut down */
    new_queue_context->watchdog_timeout = CONFIG_ROB_RECEIPT_TIMEOUT_MS;
   
    rob_ret_val_t ret_init = init_work_queue(new_queue_context, _log_prefix, queue_name);
    if (ret_init == ROB_OK) {
    
        return new_queue_context;
    } else {
        ROB_LOGE("Media queue", "Failed initiating work queue. Code : %u", ret_init);
        robusto_free(new_queue_context);
        return NULL;
    }
    
}
