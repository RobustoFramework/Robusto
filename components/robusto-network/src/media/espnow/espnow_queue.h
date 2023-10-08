/**
 * @file espnow_queue.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
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
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW



#pragma once
/*********************
 *      INCLUDES
 *********************/


#include "robusto_queue.h"
#include "robusto_peer.h"
#include "robusto_media_def.h"
/*********************
 *      DEFINES
 *********************/

#include <esp_err.h>

#ifdef __cplusplus
extern "C"
{
#endif

rob_ret_val_t espnow_init_worker(work_callback work_cb, poll_callback poll_cb, char *_log_prefix);
void espnow_set_queue_blocked(bool blocked);
queue_context_t *espnow_get_queue_context();
void espnow_shutdown_worker();
void espnow_cleanup_queue_task(media_queue_item_t *queue_item);



#ifdef __cplusplus
} /* extern "C" */
#endif
#endif
