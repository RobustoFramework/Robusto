/**
 * @file robusto_message.h
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

#include <stdint.h>
#include <stdbool.h>

#include <robusto_peer.h>
#include <robusto_media.h>
#include <robusto_queue.h>
#include <robusto_states.h>
#include <robusto_message_def.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C"
{
#endif



void robusto_message_free(robusto_message_t *message);
/**
 * @brief Get the prefix len for the specified media type
 * 
 * @param media_type 
 * @param peer 
 * @return int 
 */

int get_media_type_prefix_len(e_media_type media_type, robusto_peer_t *peer);

/**
 * Message building
 * This is about providing different ways of packaging data into a message
 * that can be sent using the framework.  
 */

uint8_t robusto_encode_message_context(message_context_t *context);
message_context_t *robusto_decode_message_context(uint8_t *message);

rob_ret_val_t send_message_strings(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                              uint8_t *strings_data, uint32_t strings_length, queue_state *state);
rob_ret_val_t send_message_binary(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                             uint8_t *binary_data, uint32_t binary_length, queue_state *state);


/**
* @brief Sends a multi message (both strings and binary) message to a specified peer on the appropriate media send queue.
 * 
 * @param peer The destination peer
 * @param service_id If set, send to this service on the per
 * @param conversation_id Tell the per to mark the related reply with this conversation id
 * @param strings_data Pointer to a null-terminated array of strings
 * @param strings_length The length of the strings data in bytes
 * @param binary_data Pointer to binary data
 * @param binary_length Pointer to length of binary data
 * @param state Optional state structure if one want to track message progress all the way to the receipt
 * @param force_media_type Force use a specified media type
 * @return rob_ret_val_t Return ROB_OK if the message was successfully put on the appropriate media send queue.
 */
rob_ret_val_t send_message_multi(robusto_peer_t *peer, uint16_t service_id, uint16_t conversation_id,
                            uint8_t *strings_data, uint32_t strings_length, uint8_t *binary_data, uint32_t binary_length, queue_state *state, e_media_type force_media_type);
/**
 * @brief Put a raw message to a specified peer on a specified media send queue. Mostly for internal use. 
 * 
 * @param peer The destination peer
 * @param media_type The media to use
 * @param data The data to send
 * @param data_length The length of the data
 * @param state Optional state structure if one want to track message progress
 * @param receipt Require a receipt for successful transfer
 * @return rob_ret_val_t Was the message successfully built and put on the queue for sending?
 */
rob_ret_val_t send_message_raw(robusto_peer_t *peer, e_media_type media_type,  uint8_t *data, uint32_t data_length, queue_state *state, bool receipt);
/**
 * @brief For internal use, like send_message_raw but takes more parameters for recursion and heartbeats
 * @internal
 * @param heartbeat This is used internally when sending heart beats. Disables retrying and other things. (TODO: Should this be something else?)
 * @param depth Recursion depth 
 * @param exclude_media_types What media types to exclude when retrying

 */
rob_ret_val_t send_message_raw_internal(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, queue_state *state, bool receipt, e_media_queue_item_type queue_item_type, uint8_t depth, uint8_t exclude_media_types, bool important);

typedef rob_ret_val_t (send_callback_cb)(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);

typedef void(poll_callback_cb)(queue_context_t * queue_context);
/**
 * @brief Called by the work callbacks of the medias
 * 
 * @param work_item The queue item to send
 * @param media_type The media type
 * @param send_callback Callback to the media send function to be used
 * @param poll_callback Callback to the media listen function
 * @param queue_context The queue context
 */
void send_work_item(media_queue_item_t * queue_item, robusto_media_t *info, e_media_type media_type, send_callback_cb *send_callback, poll_callback_cb *poll_callback, queue_context_t *queue_context);

typedef void (on_send_activity_t)(media_queue_item_t * queue_item, e_media_type media_type);

void robusto_message_sending_register_on_activity(on_send_activity_t *_on_send_activity);

/**
 * @brief Make a multi message, for sending internal, do not use for normal use
 * 
 * @param message_type 
 * @param service_id 
 * @param conversation_id 
 * @param strings_data 
 * @param strings_length 
 * @param binary_data 
 * @param binary_length 
 * @param dest_message 
 * @param no_addressing 
 * @param no_crc 
 * @return int 
 */
int robusto_make_multi_message_internal(e_msg_type_t message_type, uint16_t service_id, uint16_t conversation_id,
                               uint8_t *strings_data, uint32_t strings_length,
                               uint8_t *binary_data, uint32_t binary_length, uint8_t **dest_message);


// TODO: Centralize fragmented handling for all medias (will a stream be similar?) This is the specific fragmented case
typedef struct fragmented_message
{
    // The hash of the message, used to identify the message
    uint32_t hash;
    // We do not reuse the data pointer to:
    // 1. save memory on sender, 2. make testing in the same space possible
    // The data to send
    uint8_t *send_data;
    // The length of the message
    uint32_t send_data_length;
    // The receive buffer
    uint8_t *receive_buffer;
    // The length of the message
    uint32_t receive_buffer_length;

    // The number of fragments
    uint32_t fragment_count;
    // Size of each fragment
    uint32_t fragment_size;

    // A map of all the fragments that has been received (not automatically updated on the sender)
    uint8_t *received_fragments; // TODO: This should instead be a bitmap to save space.
    /* When the frag_msg was created, a non-used element */
    uint32_t start_time;
    /* If not 0, we have requested things to be sent again, and this is the last fragment we requested */
    uint32_t last_requested;
    /* The state of the fragment */
    e_rob_state_t state;
    /* Abort the transmission (wait 1000ms before removing structure) */
    bool abort_transmission;

    SLIST_ENTRY(fragmented_message) fragmented_messages; /* Singly linked list */

} fragmented_message_t;

/**
 * @brief Get the last used frag message object. 
 * @note Used mainly for intercepting during testing
 * 
 * @return fragmented_message_t * 
 */
fragmented_message_t * get_last_frag_message();


// A callback that can be used to send messages
/**
 * @brief 
 * 
 * @param peer A callback that can be used to send messages
 * @param data The data to send
 * @param len Length of the data
 * @param receipt Wait for a receipt
 * @return typedef The return value
 */
typedef rob_ret_val_t cb_send_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt);

/**
 * @brief Handle incoming fragmented messaged
 * 
 * @param peer The remote peer
 * @param media_type The media type
 * @param data Incoming data
 * @param len Length of data
 * @param fragment_size The fragment size of the media
 * @param send_message A callback to be able to respond
 * @return If true, send receipt (used in initialisation, but not with parts)
 */

bool handle_fragmented(robusto_peer_t *peer, e_media_type media_type, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message * send_message);


/**
 * @brief Send a message in fragments. Data must be a Robusto message
 * 
 * @param peer The destination peer
 * @param media_type The media type
 * @param data The message. Must be a valid and parsable Robusto message.
 * @param data_length The length of the message
 * @param fragment_size The size of the fragments
 * @param send_message A callback to be able to send messages
 * @return rob_ret_val_t 
 */
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, uint32_t fragment_size, cb_send_message * send_message);

/**
 * @brief A simple way to, using format strings, build a message.
 * However; as the result will have to be null-terminated, and we cannot put nulls in that string.
 * Instead, the pipe-symbol "|" is used to indicate the null-terminated sections.
 * Only one format is allowed per section: "%i Mhz|%s" is allowed. "%i Mhz, %i watts|%s" is not.
 * Integers can be passed as is.
 * IMPORTANT: 1. Do not send floating point values, format them ahead and send them as null-terminated strings.
 * This is because of how variadic functions promotes and handles these data types.
 * TODO: This function probably peaks at too much memory use to be able to work on 2k boards like the Arduino UNO,
 * it should probably not be available there.
 *
 * @param message A pointer to a pointer to the message structure, memory will be allocated to it.
 * @param format A format string where "|" indicates where null-separation is wanted and creates a section.
 * @param arg A list of arguments supplying data for the format.
 * @return int Length of message
 *
 * @note An evolution would be to identify the number of format strings in each "|"-section to enable full support of format.
 * Unclear when that woud be needed, though, perhaps if compiling error texts.
 */
int build_strings_data(uint8_t **message, const char *format, ...);

/**
 * @brief TODO: Document this
 * 
 * @param data 
 * @param data_len 
 * @param msg 
 * @param prefix_bytes
 * @return rob_ret_val_t* 
 */
rob_ret_val_t robusto_network_parse_message(uint8_t *data, uint32_t data_len, robusto_peer_t *peer, robusto_message_t **msg, int prefix_bytes);

/**
 * @brief Check a message against its CRC32 or Fletcher 16 for consistency
 * 
 * @param data The message
 * @param data_len The length of the message
 * @param prefix_bytes The number of bytes to disregard (likely addressing)
 * @return true The content is consistent with its check value
 * @return false The message is NOT consistent with its check value
 */
bool robusto_check_message(uint8_t *data, int data_len, uint8_t prefix_bytes);

/**
 * @brief Handle hi-messages
 *
 */
rob_ret_val_t robusto_handle_presentation(robusto_message_t *message);



// This section supports how the mocking backend responds to requests
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

typedef enum e_mock_msg_type {
    MMI_NONE = 0,
    MMI_STRINGS = 1,
    MMI_MULTI = 2,
    MMI_BINARY = 3,
    MMI_BINARY_RESTRICTED = 4,
    MMI_STRINGS_ASYNC = 5,
    MMI_HI = 6,
    MMI_HI_ASYNC = 7,
    MMI_SERVICE = 8,
    MMI_HEARTBEAT = 9
} e_mock_msg_type_t;

uint8_t get_message_expectation();
void set_message_expectation(uint8_t expectation);
uint8_t get_message_result();
void set_message_result(uint8_t res);

#endif

#ifdef __cplusplus
} /* extern "C" */
#endif