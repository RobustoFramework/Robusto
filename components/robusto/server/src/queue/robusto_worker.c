/**
 * @file robusto_worker.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto worker implementation.
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

#if !(defined(USE_ESPIDF) && defined(USE_ARDUINO))
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#endif

#if defined(USE_NATIVE) || (defined(USE_ARDUINO) && defined(ARDUINO_ARCH_STM32))
#include <assert.h>
#endif

#include <robusto_concurrency.h>
#include <robusto_time.h>
#include <robusto_system.h>
/* The log prefix for all logging */
static char *robusto_worker_log_prefix;

void *safe_get_head_work_item(queue_context_t *q_context)
{
    if (ROB_OK == robusto_mutex_take(q_context->__x_queue_mutex, 300))
    {
        /* Pull the first item from the work queue */
        void *curr_work = q_context->first_queue_item_cb(q_context);

        /* Immidiate deletion from the head of the queue */
        if (curr_work != NULL)
        {
            q_context->remove_first_queueitem_cb(q_context);
        }
        robusto_mutex_give(q_context->__x_queue_mutex);
        return curr_work;
    }
    else
    {
        ROB_LOGE(robusto_worker_log_prefix, "Error: Couldn't get semaphore to access work queue!");
        return NULL;
    }
}

void alter_task_count(queue_context_t *q_context, int change)
{
    if (ROB_OK == robusto_mutex_take(q_context->__x_task_state_mutex, 10000))
    {
        q_context->task_count = q_context->task_count + change;
        robusto_mutex_give(q_context->__x_task_state_mutex);
    }
    else
    {
        ROB_LOGE(robusto_worker_log_prefix, "Error: Couldn't get semaphore to alter task count (change: %i, task name: %s)!", change, q_context->worker_task_name);
    }
}

/**
 * @brief Cleanup after a queue item is handled, typically not called directly
 * @param q_context The actur
 * @param queue_item A queue item to free.
 */
void cleanup_queue_task(queue_context_t *q_context)
{
    ROB_LOGI(robusto_worker_log_prefix, "Cleaning up tasks.");
    alter_task_count(q_context, -1);
    if (q_context->multitasking)
    {
        // If we do this when it is not multitasking, we kill the entire queue worker task.
        robusto_delete_current_task();
    }
}

void log_queue_context(queue_context_t *q_context)
{
    ROB_LOGI(robusto_worker_log_prefix, "%s. More info: ", q_context->worker_task_name);
    ROB_LOGI(robusto_worker_log_prefix, "on_work_cb: %p", q_context->on_work_cb);
    ROB_LOGI(robusto_worker_log_prefix, "on_poll_cb: %p", q_context->on_poll_cb);
    ROB_LOGI(robusto_worker_log_prefix, "max_task_count: %i", q_context->max_task_count);
    ROB_LOGI(robusto_worker_log_prefix, "first_queue_item_cb: %p", q_context->first_queue_item_cb);
    ROB_LOGI(robusto_worker_log_prefix, "insert_tail_cb: %p", q_context->insert_tail_cb);
    ROB_LOGI(robusto_worker_log_prefix, "insert_head_cb: %p", q_context->insert_head_cb);
    ROB_LOGI(robusto_worker_log_prefix, "multitasking: %s", q_context->multitasking ? "true" : "false");
    ROB_LOGI(robusto_worker_log_prefix, "watchdog_timeout: %i seconds", q_context->watchdog_timeout);
    ROB_LOGI(robusto_worker_log_prefix, "---------------------------");
}

static void robusto_worker(queue_context_t *q_context)
{
    ROB_LOGI(robusto_worker_log_prefix, "Worker task now running: ");
    log_queue_context(q_context);

    robusto_watchdog_set_timeout(q_context->watchdog_timeout);

    void *curr_work = NULL;

    for (;;)
    {
        // Are we shutting down?
        if (q_context->shutdown)
        {
            break;
        }
        // First check so that the queue isn't blocked
        if (!q_context->blocked)
        {
            // Use separate tasks
            if (q_context->multitasking)
            {
                // Check that we don't have a maximum number of tasks
                if (q_context->max_task_count == 0 ||
                    // Or if we do, that we don't exceed it
                    (q_context->max_task_count > 0 && (q_context->task_count < q_context->max_task_count)))
                {
                    // Check if there is anything to do
                    curr_work = safe_get_head_work_item(q_context);
                    if (curr_work != NULL)
                    {
                        ROB_LOGI(robusto_worker_log_prefix, ">> Running multitasking callback on_work. Worker address %p", q_context->on_work_cb);
                        char taskname[30] = "\0";
                        sprintf(taskname, "%s_worker_%d", robusto_worker_log_prefix, q_context->task_count + 1);
                        /* To avoid congestion on Core 0, we act on non-immidiate requests on Core 1 (APP) */
                        alter_task_count(q_context, 1);
                        rob_ret_val_t rc = robusto_create_task((TaskFunction_t)q_context->on_work_cb, q_context, taskname, &q_context->worker_task_handle, 1);
                        if (rc != ROB_OK)
                        {
                            ROB_LOGE(robusto_worker_log_prefix, ">> Failed creating work task, returned: %i (see projdefs.h)", rc);
                            alter_task_count(q_context, -1);
                        }
                        else
                        {
                            ROB_LOGI(robusto_worker_log_prefix, ">> Created task: %s, taskcount: %i", taskname, q_context->task_count);
                        }
                    }
                }
            }
            else
            {
                // Check if there is anything to do
                curr_work = safe_get_head_work_item(q_context);
                if (curr_work != NULL)
                {
                    ROB_LOGD(robusto_worker_log_prefix, ">> Running single task callback on_work. Worker address %p, work address %p.", (void *)(q_context->on_work_cb), (void *)(curr_work));
                    q_context->on_work_cb(curr_work);
                }
                robusto_yield();
            }
        }

        /* If defined, call the poll callback. */
        if (q_context->on_poll_cb != NULL)
        {
            q_context->on_poll_cb(q_context);
        }
        robusto_yield();
    }
    ROB_LOGI(robusto_worker_log_prefix, "Worker task %s shut down, deleting task.", q_context->worker_task_name);
    // TODO: Should there be some freeing of semaphore here?
    // free(q_context->__x_queue_mutex);

    robusto_mutex_deinit(q_context->__x_queue_mutex);
    robusto_mutex_deinit(q_context->__x_task_state_mutex);

    robusto_delete_current_task();
}

rob_ret_val_t init_work_queue(queue_context_t *q_context, char *_log_prefix, const char *queue_name)
{
    robusto_worker_log_prefix = _log_prefix;

    ROB_LOGI(robusto_worker_log_prefix, "Initiating the %s work queue.", queue_name);

    if (q_context->on_work_cb == NULL)
    {
        ROB_LOGE(robusto_worker_log_prefix, "Work queue init of %s failed; no work callback defined, \
        without it there is no point to the queue.",
                 queue_name);
        return ROB_ERR_INIT_FAIL;
    }
    q_context->count = 0;

    // Default if not set
    if (q_context->normal_max_count == 0)
    {
        q_context->normal_max_count = 3;
    }
    if (q_context->important_max_count == 0)
    {
        q_context->important_max_count = 5;
    }

    /* Create semaphores to ensure thread safety (queue and tasks) */
    q_context->__x_queue_mutex = robusto_mutex_init();

    assert(q_context->__x_queue_mutex);
    q_context->__x_task_state_mutex = robusto_mutex_init();
    assert(q_context->__x_task_state_mutex);
    // Reset task count (unsafely as this must be the only initiator)
    q_context->task_count = 0;

    /* Create the xTask name. */
    strcpy(q_context->worker_task_name, "\0");
    strcpy(q_context->worker_task_name, queue_name);
    strcat(q_context->worker_task_name, " worker task");

    if (!q_context->log_prefix) {
        q_context->log_prefix = queue_name;
    }
    /** Register the worker task.
     *
     * We are running it on Core 0, or PRO as it is called
     * traditionally (cores are basically the same now)
     * Feels more reasonable to focus on comms on 0 and
     * applications on 1, traditionally called APP
     * TODO: Should we try to allocate these tasks statically using xTaskCreateStatic or/and xTaskCreateRestrictedStatic?
     */
    // TODO: Apps aren't actually run on 1? So should we move these to 1 instead?
    ROB_LOGI(robusto_worker_log_prefix, "Register the worker task. Name: %s", q_context->worker_task_name);
    /* We obviously don't immidiately want to shutdown the worker just because someone haven't set shutdown to false. */
    q_context->shutdown = false;
    int rc = robusto_create_task((TaskFunction_t)robusto_worker, q_context, q_context->worker_task_name, &q_context->worker_task_handle, 0);
    if (rc != ROB_OK)
    {
        ROB_LOGE(robusto_worker_log_prefix, "Failed creating worker task, returned: %i (see projdefs.h)", rc);
        return ROB_ERR_INIT_FAIL;
    }

    ROB_LOGI(robusto_worker_log_prefix, "Worker task registered.");

    return ROB_OK;
}
