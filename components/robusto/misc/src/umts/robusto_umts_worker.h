/**
 * @file sdp_worker.h
 * @author Nicklas Borjesson 
 * @brief The worker reacts to items appearing in the worker queue 
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#pragma once
/*********************
 *      INCLUDES
 *********************/

#include "robusto_queue.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

esp_err_t umts_init_worker(work_callback work_cb, work_callback priority_cb, char *_log_prefix);
esp_err_t umts_safe_add_work_queue(work_queue_item_t *new_item);
void umts_set_queue_blocked(bool blocked);
void umts_shutdown_worker();
void umts_cleanup_queue_task(work_queue_item_t *queue_item);



