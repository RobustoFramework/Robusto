/**
 * @file umts_worker.c
 * @author Nicklas Borjesson 
 * @brief The GSM worker maintain and monitor the GSM work queue and uses callbacks to notify the user code
 * This code uses sdp_work_queue to automatically handle queues, semaphores and tasks
 * 
 * @copyright Copyright (c) 2022
 * 
 */


#include "robusto_umts_worker.h"


#include <sys/queue.h>
#include <robusto_system.h>

#include <robusto_logging.h>
#include <string.h>

// The queue context
queue_context_t umts_queue_context;

char *umts_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(umts_work_q, umts_queue_item) umts_work_q;

struct umts_queue_item_t *umts_first_queueitem() {
    return STAILQ_FIRST(&umts_work_q); 
}

void umts_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&umts_work_q, items); 
}
void umts_insert_tail(umts_queue_item_t *new_item) {
    STAILQ_INSERT_TAIL(&umts_work_q, new_item, items);
}

rob_ret_val_t umts_safe_add_work_queue(umts_queue_item_t *new_item) {   
    return safe_add_work_queue(&umts_queue_context, new_item);
}
void umts_cleanup_queue_task(umts_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {
        robusto_free(queue_item->message);
        robusto_free(queue_item);
    }
    cleanup_queue_task(&umts_queue_context);
}

void umts_set_queue_blocked(bool blocked) {
    set_queue_blocked(&umts_queue_context,blocked);
}

void umts_shutdown_worker() {
    ROB_LOGI(umts_worker_log_prefix, "Telling gsm worker to shut down.");
    umts_queue_context.shutdown = true;
}

rob_ret_val_t umts_init_worker(work_callback work_cb, char *_log_prefix)
{
    umts_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&umts_work_q);

    umts_queue_context.first_queue_item_cb = &umts_first_queueitem; 
    umts_queue_context.remove_first_queueitem_cb = &umts_remove_first_queue_item; 
    umts_queue_context.insert_tail_cb = &umts_insert_tail;
    umts_queue_context.on_work_cb = work_cb; 
    umts_queue_context.max_task_count = 1;
    // This queue cannot start processing items until GSM is initialized
    umts_queue_context.blocked = true;

    init_work_queue(&umts_queue_context, _log_prefix, "GSM Queue");

    return ESP_OK;

}
