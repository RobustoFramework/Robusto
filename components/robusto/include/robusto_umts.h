/**
 * @file robusto_umts.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief UMTS/GSM functionality.
 * @version 0.1
 * @date 2023-10-12
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
#pragma once

#include <robconfig.h>
#include <robusto_retval.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_ROBUSTO_UMTS_SERVER
rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(char * topic, char *data);
rob_ret_val_t robusto_umts_oauth_post_form_multipart(char *url, char *data, uint32_t data_len, char *context_type, char *name, char* parent);
rob_ret_val_t robusto_umts_oauth_post_urlencode(char *url, char *data, uint32_t data_len);

unsigned int get_connection_failures();
unsigned int get_connection_successes();
int get_sync_attempts();

bool robusto_umts_sms_up();
bool robusto_umts_mqtt_up();
bool robusto_umts_ip_up();

void robusto_umts_start(char * _log_prefix);
void robusto_umts_init(char * _log_prefix);
bool umts_shutdown();

void umts_reset_rtc();


#endif


#ifdef ROBUSTO_UMTS_CLIENT

rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(TBD);
#endif