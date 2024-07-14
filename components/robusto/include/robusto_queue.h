/**
 * @file robusto_queue.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief A template queue and the base worker. TODO: This should probably be reworked somehow
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

#include <robconfig.h>

#pragma once
#include "stdint.h"
#include "stdbool.h"

// #include "robusto_media.h"

#ifdef USE_ARDUINO
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <compat/arduino_sys_queue.h>

#elif defined(USE_ESPIDF)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifndef USE_ARDUINO
#include <robusto_sys_queue.h>
#endif

#if !(defined(USE_ESPIDF) || defined(USE_ARDUINO))

#include <semaphore.h>
#endif

#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_concurrency.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if 0
/**
 * TODO: This doc is plain wrong, keeping so we don't lose any functionality.
 * The work types are:
 * HANDSHAKE: Initial communication between two peers.
 *
 * REQUEST: A peer have a request.
 * Requests are put on the work queue for consumption by the worker.
 *
 * DATA: Data, a response from a request.
 * A request can generate multible DATA responses.
 * Like requests, they are put on the work queue for consumption by the worker.
 *
 * PRIORITY: A priority message.
 * These are not put on the queue, but handled immidiately.
 * Examples may be message about an urgent problem, an emergency message (alarm) or an explicit instruction.
 * Immidiately, the peer responds with the CRC32 of the message, to prove to the reporter
 * that the message has been received before it invokes the callback.
 * In some cases it needs to know this to be able to start saving power or stop trying to send the message.
 * Emergency messages will be retried if the CRC32 doesn't match.
 *
 * ORCHESTRATION: This is about handling peers and controlling the mesh.
 * TODO: Move this into readme.md
 *
 * > 1000:
 *
 */

#endif

    // TODO: Is this and work_item ever used?
    typedef enum e_work_type
    {
        MESSAGE = 0,
        UNUSED = 1
    } e_work_type;

    typedef enum e_queue_state
    {
        QUEUE_STATE_QUEUED = 0xf0,
        QUEUE_STATE_RUNNING = 0x0f, // TODO: How should we implement a stream interface over the existing?
        QUEUE_STATE_FAILED = 0xff,  // Network stuff
        QUEUE_STATE_SUCCEEDED = 0x00,
        QUEUE_STATE_DROPPED = 0x01,
        QUEUE_STATE_QUEUEING_FAILED = 0x02,
        QUEUE_STATE_TRYING_MEDIAS = 0x03
    } e_queue_state_t;

    /** Queue state is a 3-byte array where byt
     * 1 is a progress flag
     * 2-3 is a 16-bit integer holding a result value
     * How this value is handled is down to the queue implementation.
     **/
    typedef uint8_t queue_state[3];

    /**
     * @brief This is the request queue
     * The queue is served by worker tasks in a thread-safe manner.
     */

    typedef struct queue_item
    {
        /* The type of work */
        enum e_work_type work_type;

        /* Data payload */
        void *data;
        /* Queue reference */
        STAILQ_ENTRY(queue_item)
        items;
    } work_queue_item_t;

    typedef struct queue_context queue_context_t;

    typedef void *(first_queueitem)(queue_context_t *q_context);
    typedef void(remove_first_queueitem)(queue_context_t *q_context);

    typedef void(insert_queue_item)(queue_context_t *q_context, void *new_item);

    typedef void(work_callback)(void *q_work_item);

    typedef void(poll_callback)(void *q_context);

    typedef struct queue_context
    {
        /* Queue management callbacks, needed because of the difficulties in passing queues as pointers */
        first_queueitem *first_queue_item_cb;
        remove_first_queueitem *remove_first_queueitem_cb;
        insert_queue_item *insert_tail_cb;
        insert_queue_item *insert_head_cb;
        /* Mandatory callback that handles incoming work items */
        work_callback *on_work_cb;

        /* Log prefix */
        char *log_prefix;

        /* Optional callback that is called each poll period by the worker.
        Note: This is run in the queue task, and might conflict with multitasking. */
        poll_callback *on_poll_cb;
        /* Max number of concurrent tasks. (0 = unlimited) */
        uint8_t max_task_count;
        /* Current number of running tasks */
        uint8_t task_count;
        /* Create separate tasks for each work.
        Do not do this if the functionality isn't thread safe.
        Also, what is gained in isolation, might be lost in performance. */
        bool multitasking;
        /* Worker task */
        rob_task_handle_t worker_task_handle;
        /* Worker task name*/
        char worker_task_name[50];

        void *work_queue;

        /* The current number of items */
        uint8_t count;
        /* The max number of items where normal items are dropped */
        uint8_t normal_max_count;
        /* The current number of items where important items are dropped */
        uint8_t important_max_count;
        /* If set, the queue will not process any items */
        bool blocked;
        /* If set, worker will shut down */
        bool shutdown;

        /* Watchdog timeout in seconds */
        int watchdog_timeout;

        /* Mutexes for thread safety*/
        mutex_ref_t __x_queue_mutex;      // Thread-safe the queue
        mutex_ref_t __x_task_state_mutex; // Thread-safe the tasks
    } queue_context_t;

    rob_ret_val_t safe_add_work_queue(queue_context_t *q_context, void *new_item, bool important);

    rob_ret_val_t init_work_queue(queue_context_t *q_context, char *_log_prefix, const char *queue_name);

    void set_queue_blocked(queue_context_t *q_context, bool blocked);

    void cleanup_queue_task(queue_context_t *q_context);

    /**
     * The Robusto queue state exploits the fact that eading and writing a single byte is an atomic operation on all relevant architectures
     * So the queue is get a very light-weight semaphore that can bring with it a 16-bit integer rob_ret_val_t.
     */

    /**
     * @brief Sets a Robusto flag to failed .
     *
     * @param flag A pointer to the flag
     * @param return_value A pointer to where to store the return value. Can be null.
     */
    void robusto_set_queue_state_result(queue_state *state, rob_ret_val_t return_value);

    /**
     * @brief If the result value is OK, it sets the flag to queued
     *
     * @param flag The flag
     * @param retval The return value of the attempt to add it to a queue
     * @return rob_ret_val_t passing on the retval
     */
    rob_ret_val_t robusto_set_queue_state_queued_on_ok(queue_state *state, rob_ret_val_t retval);

    /**
     * @brief Sets a Robusto flag to running.
     *
     * @param flag A pointer to the flag
     */
    void robusto_set_queue_state_running(queue_state *state);

    /**
     * @brief Sets a Robusto flag to trying.
     *
     * @param flag A pointer to the flag
     */
    void robusto_set_queue_state_trying(queue_state *state);

    /**
     * @brief Wait for robusto flag to either timeout, fail or succeed
     * (Basically we wait for a byte to change value from 0x0f or 0xf0 to something else)
     *
     * @param flag
     * @param timeout_ms
     * @param return_value
     * @return true
     * @return false
     */
    bool robusto_waitfor_queue_state(queue_state *state, uint32_t timeout_ms, rob_ret_val_t *return_value);
    /**
     * @brief Free a queue state and return NULL if state is either failed or succeeded.
     *
     * @param state
     * @return queue_state*
     */
    queue_state *robusto_free_queue_state(queue_state *state);

#ifdef __cplusplus
} /* extern "C" */
#endif
