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

#include <robconfig.h>

#include <robusto_media_def.h>
#include <robusto_message.h>
/*********************
 *      DEFINES
 *********************/


typedef struct umts_queue_item
{
    /* The MQTT topic */
    char * topic;
    /* The data */
    char * data;
    /* The queue item state */  
    queue_state *state;
    /* Queue reference */
    STAILQ_ENTRY(umts_queue_item)
    items;
} umts_queue_item_t;

typedef void(umts_callback)(umts_queue_item_t *queue_item);

rob_ret_val_t umts_init_queue(work_callback work_cb, char *_log_prefix);
rob_ret_val_t umts_safe_add_work_queue(umts_queue_item_t *new_item);
void umts_set_queue_blocked(bool blocked);
void umts_shutdown_queue();
void umts_cleanup_queue_task(umts_queue_item_t *queue_item);



