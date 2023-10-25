#pragma once
#include <robconfig.h>
#include <stdint.h>
#include <robusto_retval.h>
#include <robusto_queue.h>

#ifndef USE_ARDUINO
#include "sys/queue.h"
#endif


typedef struct sample_queue_item
{
    /* A test value */
    int test_value;
    /* Queue reference */
    STAILQ_ENTRY(sample_queue_item)
    items;
} sample_queue_item_t;

/* Callbacks that handles incoming work items */
typedef void (sample_queue_callback)(sample_queue_item_t *work_item);

rob_ret_val_t sample_safe_add_work_queue(int test_value);

rob_ret_val_t sample_init_worker(sample_queue_callback work_cb, poll_callback poll_cb, char *_log_prefix);

void sample_set_queue_blocked(bool blocked);

void sample_shutdown_worker();

queue_context_t *sample_get_queue_context();

