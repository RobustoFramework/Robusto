/**
 * @file robusto_pubsub.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief This is a publish-subscribe implementation Robusto-style
 
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

#pragma once
#include <robconfig.h>
#if defined(CONFIG_ROBUSTO_PUBSUB_SERVER) ||  defined(CONFIG_ROBUSTO_PUBSUB_CLIENT)

#ifdef __cplusplus
extern "C"
{
#endif
#define PUBSUB_SUBSCRIBE 0U
#define PUBSUB_SUBSCRIBE_RESPONSE 1U
#define PUBSUB_UNSUBSCRIBE 4U
#define PUBSUB_UNSUBSCRIBE_RESPONSE 5U
#define PUBSUB_PUBLISH 8U
#define PUBSUB_PUBLISH_UNKNOWN_TOPIC 9U
#define PUBSUB_DATA 12U
#define PUBSUB_GET_TOPIC 16U
#define PUBSUB_GET_TOPIC_RESPONSE 17U

#define PUBSUB_CLIENT_ID 1957U
#define PUBSUB_SERVER_ID 1958U


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
