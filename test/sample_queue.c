#include "sample_queue.h"
#ifdef USE_ARDUINO
#include "compat/arduino_sys_queue.h"
#else
#include <sys/queue.h>
#include <stddef.h>
#include <stdlib.h>
#endif

#include <robusto_logging.h>

// The queue context
queue_context_t *sample_queue_context;

char *sample_worker_log_prefix;

/* Expands to a declaration for the work queue */
STAILQ_HEAD(sample_work_q, sample_queue_item) 
sample_work_q;

void *sample_first_queueitem() 
{
    return STAILQ_FIRST(&sample_work_q); 
}

void sample_remove_first_queue_item(){
    STAILQ_REMOVE_HEAD(&sample_work_q, items);
}

void sample_insert_tail(void *new_item) { 
    sample_queue_item_t *item = new_item;
    STAILQ_INSERT_TAIL(&sample_work_q, item, items);
}


rob_ret_val_t sample_safe_add_work_queue(int test_value) { 
    sample_queue_item_t *new_item = malloc(sizeof(sample_queue_item_t)); 
    new_item->test_value = test_value;
    return safe_add_work_queue(sample_queue_context, new_item);
}
void sample_cleanup_queue_task(sample_queue_item_t *queue_item) {
    if (queue_item != NULL)
    {    

        free(queue_item);
    }
    cleanup_queue_task(sample_queue_context);
}

queue_context_t *sample_get_queue_context() {
    return sample_queue_context;
}

void sample_set_queue_blocked(bool blocked) {
    set_queue_blocked(sample_queue_context,blocked);
}

void sample_shutdown_worker() {
    ROB_LOGI(sample_worker_log_prefix, "Telling sample worker to shut down.");
    sample_queue_context->shutdown = true;
}

rob_ret_val_t sample_init_worker(sample_queue_callback work_cb, poll_callback poll_cb, char *_log_prefix)
{
    sample_worker_log_prefix = _log_prefix;
    // Initialize the work queue
    STAILQ_INIT(&sample_work_q);
    sample_queue_context = malloc(sizeof(queue_context_t));

    sample_queue_context->first_queue_item_cb = sample_first_queueitem; 
    sample_queue_context->remove_first_queueitem_cb = sample_remove_first_queue_item; 
    sample_queue_context->insert_tail_cb = sample_insert_tail;
    sample_queue_context->on_work_cb = work_cb; 
    sample_queue_context->on_poll_cb = poll_cb;
    sample_queue_context->max_task_count = 1;
    // This queue cannot start processing items until we want the test to start
    sample_queue_context->blocked = true;
    sample_queue_context->multitasking = false;
    sample_queue_context->watchdog_timeout = 7;
    sample_queue_context->shutdown = false;
    sample_queue_context->log_prefix = _log_prefix;

    return init_work_queue(sample_queue_context, _log_prefix, "Sample queue");      
}

