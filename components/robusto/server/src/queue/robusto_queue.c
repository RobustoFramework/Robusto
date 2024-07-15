/**
 * @file robusto_queue.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto queue implementation.
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
#include <robusto_queue.h>

#include <robusto_concurrency.h>
#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <string.h>

/* All state function doesn't to anything if the state pointer is null */

void robusto_set_queue_state_result(queue_state *state, rob_ret_val_t return_value) {
    if (state == NULL) { 
        ROB_LOGD("----", "robusto_set_queue_state_result - State is NULL");
        return; }
    // No one is interested in the result, and asks for it to be freed.
    if (*state[0] == QUEUE_STATE_DROPPED) {
        ROB_LOGW("----", "Queue item marked as dropped, freeing memory");
        robusto_free(state);
    } else
    if (return_value == ROB_OK) {
        *state[0] = QUEUE_STATE_SUCCEEDED;
    } else {
        /* We set the result first, as that is the only thing that is NOT randomly accessed by several tasks until the state is set. */
        memcpy(state + 1, &return_value, sizeof(rob_ret_val_t));
        /* NOTE: 8 bit value access is implicitly atomic on all relevant platforms, and can thus be set like this */
        /* meaning it is thread safe. */
        ROB_LOGE("----", "Queue item marked as failed.");
        *state[0] = QUEUE_STATE_FAILED;
    }
}
rob_ret_val_t robusto_set_queue_state_queued_on_ok(queue_state *state, rob_ret_val_t retval) {
    if (state == NULL) { 
        return retval; }
    if (retval == ROB_OK) {
        *state[0] = QUEUE_STATE_QUEUED;
    } else {
        *state[0] = QUEUE_STATE_QUEUEING_FAILED;
    }
    // Regardless, forward the return value.
    return retval;
}


queue_state *robusto_free_queue_state(queue_state *state) {
    if (state == NULL) { 
        return NULL; 
    }
    
    if ((*state[0] == QUEUE_STATE_FAILED) || 
        (*state[0] == QUEUE_STATE_SUCCEEDED) || 
        (*state[0] == QUEUE_STATE_DROPPED) || 
        (*state[0] == QUEUE_STATE_QUEUEING_FAILED)) {
        robusto_free(state);
        return NULL;
    } else {
        ROB_LOGW("----", "Queue state still queued or running. Marking queue item as dropped.");
        // Still running, we are note interested though, so just free when dobe
        *state[0] = QUEUE_STATE_DROPPED;
        return state;
    }
    
};

void robusto_set_queue_state_queued(queue_state *state) {
    if (state == NULL) { return; }
    *state[0] = QUEUE_STATE_QUEUED;
};

void robusto_set_queue_state_running(queue_state *state) {
    if (state == NULL) { return; }
    *state[0] = QUEUE_STATE_RUNNING;  
};

void robusto_set_queue_state_trying(queue_state *state) {
    if (state == NULL) { return; }
    *state[0] = QUEUE_STATE_TRYING_MEDIAS;  
};


bool robusto_waitfor_queue_state(queue_state *state, uint32_t timeout_ms, rob_ret_val_t *return_value) {
    if (state == NULL) { return false; }
    int starttime = r_millis();
    while ((*state[0] == QUEUE_STATE_RUNNING || *state[0] == QUEUE_STATE_QUEUED|| *state[0] == QUEUE_STATE_TRYING_MEDIAS) 
        && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        *return_value = ROB_ERR_TIMEOUT;
        return false;
    } else {
        memcpy(return_value, state + 1, sizeof(rob_ret_val_t));
        return *state[0] == QUEUE_STATE_SUCCEEDED ? true:false;
    }
}


rob_ret_val_t safe_add_work_queue(queue_context_t *q_context, void *new_item, bool important)
{
    if (q_context->shutdown)
    {
        ROB_LOGE(q_context->log_prefix, "The queue is shut down.");
        return ROB_ERR_MUTEX;
    } 
    
    if (q_context->count > q_context->normal_max_count) {
        if (!important) {
            ROB_LOGI(q_context->log_prefix, "The queue is full, dropping normal message.");
            return ROB_ERR_QUEUE_FULL;
        } else 
        if (q_context->count > q_context->important_max_count) {
            ROB_LOGE(q_context->log_prefix, "The queue is full, dropping important message.");
            return ROB_ERR_QUEUE_FULL;
        } 
    } 

    if (ROB_OK == robusto_mutex_take(q_context->__x_queue_mutex, (q_context->watchdog_timeout-1) * 1000)) // TODO: Not fond of max delay here, what should it be?
    {
        /* As the worker takes the queue from the head, and we want a LIFO, add the item to the tail */
        q_context->insert_tail_cb(q_context, new_item);
        robusto_mutex_give(q_context->__x_queue_mutex);
    }
    else
    {
        ROB_LOGE(q_context->log_prefix, "Couldn't get semaphore to add to work queue!");
        return ROB_ERR_MUTEX;
    }
    
    return ROB_OK;
}


void set_queue_blocked(queue_context_t *q_context, bool blocked)
{
    if (ROB_OK == robusto_mutex_take(q_context->__x_queue_mutex, 10000))
    {
        q_context->blocked = blocked;
        robusto_mutex_give(q_context->__x_queue_mutex);
    }
    else
    {

        ROB_LOGE(q_context->log_prefix, "Error: Couldn't get semaphore to block/unblock it (blocked: %i, task_name: %s)!", (int)blocked, q_context->worker_task_name);
    }
}




