/**
 * @file robusto_message_def.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Provides the Robusto messaging interface
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

#include <robusto_peer_def.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C"
{
#endif
/* The current protocol version */
#define ROBUSTO_PROTOCOL_VERSION 0x00

/* Lowest supported protocol version */
#define ROBUSTO_PROTOCOL_VERSION_MIN 0x00

/* We always have a byte that contains the context */
#define ROBUSTO_CONTEXT_BYTE_LEN 1

/**
 * @todoBelow list is not correct, disregard
 * The types of messages are:
 * 
 * MSG_PRESENTATION: The initial contact between two peers.
 * A peer sends information about itself to another peer. 
 * Normally, the other peer would respond with the same information
 * 
 * MSG_MESSAGE: A normal message, sent to an app to another. 
 * Results in a immidate acknowledgement from the other peer, indicating CRC32 ok or not.
 * 
 * MSG_CALL: The message is destined to some service on the destination
 * 
 * MSG_FORWARD_MESSAGE: This is a message that is supposed to be forwarded.
 * It has a hop count and an array of outgoing relationids that describes the route to the receiver. 
 * 
 * MSG_FORWARD_ACK: This is a acknowledgement of a forwarded message. 
 * It has the opposite array of relationid carries information if the message got there or not.
 * Note that a peer that gets this needs to match with its outgoing RelationId.
 * If the hop count is zero, it will send a normal ACK to the sender.
 * 
 * MSG_STREAM: This message is a data stream. 
 * It doesn't wait for acknowlegdement, and the peer will not send one. 

 * 
 * MSG_HELP_LISTEN: A request to help out with listening for a certain relationid. 
 * If it gets a message with that id, it will create a forward message with one hop and re-send the ack it gets in response.
 * A peer that gets help listening, will handle messages a bit differently, it will not respond directly to a relationid unless it gets a perfect message (CRC32 OK).
 * If the helper gets a perfect message, it will respond to that with a MSG_FORWARD_ACK with a hop count of zero.
 * 
 * 
 */

typedef enum e_msg_type
{
    MSG_MESSAGE     = 0, // Normal messages
    MSG_STREAM      = 1, // TODO: How should we implement a stream interface over the existing?
    MSG_NETWORK     = 2, // Network messages, like presentations. No receipts.
    MSG_HEARTBEAT   = 3, // The heartbeat, sent to peers to check if an idle connection works. No receipt.
    MSG_FRAGMENTED  = 4, // Fragmented messaging
    MSG_UNUSED_1    = 5,
    MSG_UNUSED_2    = 6,
    MSG_UNUSED_3    = 7
} e_msg_type_t;


/**
 * @brief Byte-values used by network type messages
 */
typedef enum e_network_request 
{
    /* A HI-message */
    NET_HI          = 0x00,
    /* A response to a HI-message */ 
    NET_HIR         = 0x01,
    /* A request to help with listening */
    NET_HELP_LISTEN = 0x02
} e_network_request_t;

/**
 * @brief Byte-values used by the fragmentaton communication
 */
typedef enum e_fragment_request 
{
    /* This means that a fragmented message transmission will follow */
    FRAG_REQUEST      = 0x00,
    /* This is a part of a fragmented message transmission */
    FRAG_MESSAGE  = 0x01,
    /* Asking the receiver the state of the request (if no result is received before the timeout or if progress is needed) */ 
    FRAG_CHECK   = 0x02,
    /* This is the success message from the receiver */ 
    FRAG_RESEND   = 0x05,
    /* This is the success message from the receiver */ 
    FRAG_RESULT   = 0x06,

} e_fragment_request_t;

/* This is information that is encoded into a byte and sent with every message.  */
typedef struct message_context
{
    /* The message type */
    e_msg_type_t message_type;
    /* It is a call that is directed to some service on the peer. 
    This indicates that there will be a one-byte service id just after this context byte. */
    bool is_service_call;
    /* It is a conversation, and will have a conversation index */
    bool is_conversation;
    /* The message has a part with an array of null-terminated strings */
    bool has_strings;
    /* The message has a binary part. If it also has strings, this indicates that there is a two-byte string length before the strings. */
    bool has_binary;

    // TODO: Implement forwarding. Is OSPF Something to consider? Two-way? Fixed routes?

} message_context_t;

typedef struct robusto_message
{
    /* The conversation it belongs to */
    uint16_t conversation_id;
    /* Message context */
    message_context_t context;
    /* The data */
    uint8_t *raw_data;
    /* The length of the data in bytes */
    uint32_t raw_data_length;
    /* Strings data as an array of null-terminated strings */
    char **strings;
    /* The number of strings */
    uint16_t string_count;
    /* The data */
    uint8_t *binary_data;
    /* The length of the data in bytes */
    uint32_t binary_data_length;
    /* The relevant peer, destination if outgoing, source if incoming */  
    robusto_peer_t *peer;
    /* The media type that sent the message */
    e_media_type media_type;
    /* The data */
    uint16_t service_id;

} robusto_message_t;

#ifdef __cplusplus
} /* extern "C" */
#endif