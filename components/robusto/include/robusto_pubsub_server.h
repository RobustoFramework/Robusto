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
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER



#include <stdint.h>
#include <stdbool.h>
#include <robusto_retval.h>
#include <robusto_peer_def.h>


#ifdef __cplusplus
extern "C"
{
#endif

typedef void (pubsub_server_subscriber_callback)(uint8_t *data, uint16_t data_length);

typedef struct pubsub_server_subscriber pubsub_server_subscriber_t;

/* A subscriber, forms a linked list */
struct pubsub_server_subscriber {
    /* If set, send data to peer */
    robusto_peer_t * peer;
    /* If set, call a local callback with the data */
    pubsub_server_subscriber_callback * local_callback;
    /* Next subscriber */
    pubsub_server_subscriber_t * next;
};

typedef struct pubsub_server_topic pubsub_server_topic_t;

/* A topic, forms a linked list */
struct pubsub_server_topic {
    /* The name of the topic, typically dot separated a.c.1, like nmea.pgn.1223423 */
    char * name;
    /* The hash of the topic */
    uint32_t hash;
    /* Message count */
    uint8_t count;
    /* Subscriber count */
    uint8_t subscriber_count;
    /* First of the linked list of subscribers */
    pubsub_server_subscriber_t *first_subscriber;
    /* Last of the linked list of subscribers */
    pubsub_server_subscriber_t *last_subscriber;
    /* The next topic */
    pubsub_server_topic_t *next;
};

/**
 * @brief Register a subscription to a topic
 * @note Will create a new topic if non-existing
 * 
 * @param peer The peer wishing to subscribe
 * @param topic The name of the topic, i.e. nmea.134532 or internal.temp
 * @return uint32_t 
 */
uint32_t robusto_pubsub_server_subscribe(robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb, char * topic_name);

/**
 * @brief Unregister a subscription
 * 
 * @param peer The peer wishing to unsubscribe
 * @param topic The hash of the topic to unsubcribe from
 * @return rob_ret_val_t Result of unsubscription 
 */
uint32_t robusto_pubsub_server_unsubscribe(robusto_peer_t *peer, pubsub_server_subscriber_callback *local_cb, uint32_t topic);

/**
 * @brief Create a new topic or return a matching one
 * 
 * @param name A null-terminated string of the with the name (i.e. nmea.12345)
 * @return upubsub_topic_t A topic
 */
pubsub_server_topic_t * robusto_pubsub_server_find_or_create_topic(char * name);

/**
 * @brief Publish data
 * 
 * @param topic_hash The topic hash
 * @param data The payload
 * @param data_length The length of the data
 * @return rob_ret_val_t ROB_OK if successful
 */
rob_ret_val_t robusto_pubsub_server_publish(uint32_t topic_hash, uint8_t *data, uint16_t data_length);

/**
 * @brief Start the pubsub service
 * 
 * @return rob_ret_val_t 
 */
rob_ret_val_t robusto_pubsub_server_start();

/**
 * @brief Initate pub sub service
 * 
 * @param _log_prefix 
 * @return rob_ret_val_t 
 */
rob_ret_val_t robusto_pubsub_server_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
