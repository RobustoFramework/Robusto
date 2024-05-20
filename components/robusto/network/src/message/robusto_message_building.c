/**
 * @file robusto_message_building.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto messaging building functionality
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

#include <robusto_message.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_network_init.h>
#include <robusto_concurrency.h>
#include <inttypes.h>

/* We reserve the first byte that we use for internal signaling.
 * For example, when data has been transmitted, and how it went. */

static char *message_building_log_prefix;
/**
 * @brief Encode the message context struct into a byte
 *
 * @param context The message context
 * @return uint8_t The byte value
 */
uint8_t robusto_encode_message_context(message_context_t *context)
{

    ROB_LOGD(message_building_log_prefix, "message_type: %hhx, is_service_call: %s, is_conversation: %s, has_strings: %s,  has_binary: %s",
             context->message_type,
             context->is_service_call ? "true" : "false",
             context->is_conversation ? "true" : "false",
             context->has_strings ? "true" : "false",
             context->has_binary ? "true" : "false");

    // First set the message type, it will occupy the first three bytes.
    uint8_t res_ctxt = context->message_type;
    if (context->is_service_call)
    {
        res_ctxt |= 1 << 3;
    }
    // The 4th byte says if it is a conversation, a conversation id will then be found further into the message
    if (context->is_conversation)
    {
        res_ctxt |= 1 << 4;
    }
    // Does it have a section with null-terminated strings? It will then have a string length later in the message
    if (context->has_strings)
    {
        res_ctxt |= 1 << 5;
    }
    // Does it have a binary section? If so, that will end the message.
    if (context->has_binary)
    {
        res_ctxt |= 1 << 6;
    }
    ROB_LOGD(message_building_log_prefix, "res_ctxt : %hhx", res_ctxt);
    return res_ctxt;
}

int robusto_make_multi_message_internal(e_msg_type_t message_type, uint16_t service_id, uint16_t conversation_id,
                               uint8_t *strings_data, uint32_t strings_length,
                               uint8_t *binary_data, uint32_t binary_length, uint8_t **dest_message)
{

    ROB_LOGD(message_building_log_prefix, "In robusto_make_multi_message. message_type: %hu, service_id: %u, conversation_id: %u, strings_length: %lu, binary_length: %lu.",
     message_type, service_id, conversation_id, strings_length, binary_length);
    /**
     * We always have:
     * 1. Some media-dependent prefix data (probably source address or relation id)
     * 2. A 4-bytes CRC32
     * 3. A 1-byte message context
     * 4. A 16-byte encryption pad reference (not implemented = 0)
     */
    uint32_t message_length = ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN + 0;
    struct message_context context = {
        .message_type = message_type,
        .is_service_call = (service_id > 0),
        .is_conversation = (conversation_id > 0),
        .has_strings = (strings_length > 0),
        .has_binary = (binary_length > 0)

    };
    /* To avoid unnecessary memory allocations, we first add up how long the message data will be. */
    /* It is a service call, the service_id takes 1 byte. */
    uint8_t service_offset = 0;
    if (context.is_service_call)
    {
        service_offset = sizeof(service_id);
        message_length += service_offset;
    }

    // If we have a conversation id, it takes 2 bytes
    uint8_t conversation_offset = 0;
    if (context.is_conversation)
    {
        conversation_offset = sizeof(conversation_id);
        message_length += conversation_offset;
    }

    // If we have a string part, add its length
    if (context.has_strings)
    {
        message_length += strings_length;
    }

    uint8_t strings_offset = 0;
    // If we have a binary part, add its length
    if (context.has_binary)
    {
        message_length += binary_length;
        // If we have a binary part *and* a strings part we need to add a two-byte length of the strings.
        if (context.has_strings)
        {
            strings_offset = 2;
            message_length += strings_offset;
        }
    }
    ROB_LOGD(message_building_log_prefix, "Allocating %lu bytes for message.", message_length);
    *dest_message = robusto_malloc(message_length);
    (*dest_message)[ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH] = robusto_encode_message_context(&context);
    ROB_LOGD(message_building_log_prefix, "Context %hu", (*dest_message)[ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH]);
    uint8_t base_offset = ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN;
    if (context.is_service_call)
    {
        memcpy(*dest_message + base_offset, &service_id, sizeof(service_id));
    }
    if (context.is_conversation)
    {
        memcpy(*dest_message + base_offset + service_offset, &conversation_id, sizeof(conversation_id));
    }

    if (strings_offset > 0)
    {
        // We need to add a length before the strings
        memcpy(*dest_message + base_offset + service_offset + conversation_offset, &strings_length, sizeof(strings_length));
    }
    if (context.has_strings)
    {
        // If we have strings, put them in the message
        memcpy(*dest_message + base_offset + service_offset + conversation_offset + strings_offset, strings_data, strings_length);
    }

    if (context.has_binary)
    {
        // If we have a binary payload, add it to the end.
        memcpy(*dest_message + message_length - binary_length, binary_data, binary_length);
    }

    // TODO: Encrypt the message here, before the CRC check (which should NOT be encrypted).
    
    // Calculate CRC32 of the finished message and put after any prefix bytes.
    uint32_t crc32 = robusto_crc32(0, *dest_message + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, message_length - ROBUSTO_CRC_LENGTH - ROBUSTO_PREFIX_BYTES);
    ROB_LOGD(message_building_log_prefix, "CRC32 dec: %lu", crc32);
    memcpy(*dest_message + ROBUSTO_PREFIX_BYTES, &crc32, ROBUSTO_CRC_LENGTH);
    return message_length;
}

int build_strings_data(uint8_t **message, const char *format, ...)
{

    va_list arg;
    va_start(arg, format);

    int format_len = strlen(format);

    char *loc_format = (char *)robusto_malloc(format_len + 1);
    strcpy(loc_format, format);
    ROB_LOGD(message_building_log_prefix, "Format string %s len %i", loc_format, format_len);

    int break_count = 1;
    /* Make a pass to count pipes and replace them with nulls */
    for (int i = 0; i < format_len; i++)
    {
        if ((int)(loc_format[i]) == 124)
        {
            loc_format[i] = 0;
            break_count++;
        }
    }
    // Ensure null-termination (alloc is 1 longer than format len)
    ROB_LOGD(message_building_log_prefix, "format_len %i", format_len);
    loc_format[format_len] = 0;
    /* Now we know that our format array needs to be break_count long, allocate it */
    char **format_array = robusto_malloc(break_count * sizeof(char *));

    format_array[0] = loc_format;
    int format_count = 1;
    for (int j = 0; j < format_len; j++)
    {
        if ((int)(loc_format[j]) == 0)
        {

            format_array[format_count] = (char *)(loc_format + j + 1);
            format_count++;
        }
    }
    int curr_pos = 0;
    size_t new_length = 0;
    char *value_str = NULL;
    int value_length = 0;
    /* Now loop all formats, and put the message together, reallocating when enlarging */
    char *curr_format;
    for (int k = 0; k < format_count; k++)
    {
        curr_format = (char *)(format_array[k]);
        // Is there any formatting at all in the format string?
        if (strchr(curr_format, '%') != NULL)
        {
            // Fetch the next parameter.
            void *value = va_arg(arg, void *);
            value_length = robusto_asprintf(&value_str, curr_format, (void *)value);
        }
        else
        {
            // If the format string doesn't contain a %-character, just use the format for value
            value_length = strlen(curr_format);
            value_str = malloc(value_length);
            strncpy(value_str, curr_format, value_length);
        }
        new_length += value_length + 1;

        // TODO: See if realloc has any significant performance impact, if so an allocations strategy might be useful

        if (k == 0)
        {
            *message = robusto_malloc(new_length);
        }
        else
        {
            *message = robusto_realloc(*message, new_length);
        }

        if (*message == NULL)
        {
            ROB_LOGE(message_building_log_prefix, "(Re)alloc failed.");
            new_length = -ROB_ERR_OUT_OF_MEMORY;
            goto cleanup;
        };
        memcpy((*message) + curr_pos, value_str, value_length);

        (*message)[new_length - 1] = (uint8_t)0x00;
        ROB_LOGD(message_building_log_prefix, "Message: %s, value_str: %s, new_length: %i.", (char *)*message, value_str, (int)new_length);
        robusto_free(value_str);
        value_str = NULL;
        curr_pos = new_length;
    }
    rob_log_bit_mesh(ROB_LOG_DEBUG, message_building_log_prefix, *message, new_length);
cleanup:
    va_end(arg);
    robusto_free(value_str);
    robusto_free(loc_format);
    robusto_free(format_array);
    return new_length;
}

void robusto_message_building_init(char *_log_prefix)
{
    message_building_log_prefix = _log_prefix;
}