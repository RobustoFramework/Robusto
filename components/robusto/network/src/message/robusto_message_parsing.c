/**
 * @file robusto_message_building.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto message parsing
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

#include <stdint.h>
#include <robusto_message.h>
#include <robusto_network_init.h>

#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <inttypes.h>
#include <string.h>
static char * message_parsing_log_prefix = "Message parsing not initiated!";

bool robusto_check_message(uint8_t *data, int data_len, uint8_t prefix_bytes) {
    ROB_LOGD(message_parsing_log_prefix, "In robusto_check_message, prefix bytes: %"PRIu8"", prefix_bytes);

    // After the prefix (likely addressing), the message begins with a CRC32 or a CRC16 followed by 
    uint32_t crc32 = 0;

    memcpy(&crc32, &(data[prefix_bytes]), ROBUSTO_CRC_LENGTH);
    // Calculate it for rest of the message

    uint32_t calc_crc32 = robusto_crc32(0, data + prefix_bytes + ROBUSTO_CRC_LENGTH, data_len - prefix_bytes - ROBUSTO_CRC_LENGTH);
    bool crc_failed = (crc32 != calc_crc32);
    if (crc_failed) {
        // Check so that bytes 3 and 4 aren't 0, which means that this it is a 16-bit calc_fletcher16 checksum used by very resource restricted peers.
        if (!((data[prefix_bytes + 2] == 0) && (data[prefix_bytes + 3] == 0))) {
            ROB_LOGE(message_parsing_log_prefix, "Got a bad CRC32, message said %"PRIu32", but the message had %"PRIu32" with prefix_bytes: %hu. Stops parsing.", crc32, calc_crc32, prefix_bytes);
            return false;
        } else {
            // So it is a 16 bit CRC. Let's check that as well.
            uint16_t fletcher16 = 0;
            memcpy(&fletcher16, data +prefix_bytes ,2);
            uint16_t calc_fletcher16;
            robusto_checksum16(data + prefix_bytes + ROBUSTO_CRC_LENGTH, data_len - prefix_bytes - ROBUSTO_CRC_LENGTH, &calc_fletcher16);
            if (fletcher16 != calc_fletcher16) {
                ROB_LOGE(message_parsing_log_prefix, "Got a bad fletcher16 checksum, message said %"PRIu16", but the message had %"PRIu16". Stops parsing.", fletcher16, calc_fletcher16);
                return false;
            } else {
             //   ROB_LOGI(message_parsing_log_prefix, "Got a GOOD fletcher16 checksum, message said %"PRIu16", but the message had %"PRIu16". Stops parsing.", fletcher16, calc_fletcher16);
            }
        } 
    }
    return true;

}

rob_ret_val_t robusto_network_parse_message(uint8_t *data, uint32_t data_len, robusto_peer_t *peer, robusto_message_t **msg, int prefix_bytes) 
{
    ROB_LOGD(message_parsing_log_prefix, "In robusto_network_parse_message, prefix bytes: %"PRIu8"", prefix_bytes);
        
    // TODO: Decide on calling it prefix_bytes or offset.
    *msg = robusto_malloc(sizeof(robusto_message_t));    
    robusto_message_t *msg_inst = *msg;
    msg_inst->raw_data = data;
    msg_inst->raw_data_length = data_len;
    msg_inst->peer = peer;
    ROB_LOGD(message_parsing_log_prefix, "%"PRIu32" bytes memory allocated. Parsing context.", data_len);
    
    // Then, there is the message context. 
    uint8_t first_byte = data[prefix_bytes + ROBUSTO_CRC_LENGTH];
    // TODO: Stop offsetting for CRC if is CAN bus. 
    msg_inst->context.message_type = ((first_byte >> 0) & 1) + (((first_byte >> 1) & 1) * 2) + (((first_byte >> 2) & 1) * 4);
    msg_inst->context.is_service_call = ((first_byte >> 3) & 1);
    msg_inst->context.is_conversation = ((first_byte >> 4) & 1);
    msg_inst->context.has_strings = ((first_byte >> 5) & 1);
    msg_inst->context.has_binary = ((first_byte >> 6) & 1);
    
    ROB_LOGD(message_parsing_log_prefix, "Context(hex): %hhx, message_type: %hhx, is_service_call: %s, is_conversation: %s, has_strings: %s,  has_binary: %s",
    first_byte,
    msg_inst->context.message_type,
    msg_inst->context.is_service_call ? "true" : "false",
    msg_inst->context.is_conversation ? "true" : "false",
    msg_inst->context.has_strings ? "true" : "false",
    msg_inst->context.has_binary ? "true" : "false");
    
    /* curr_pos indicates how far we've come in the parsing */
    uint32_t curr_pos = prefix_bytes + ROBUSTO_CRC_LENGTH + 1;
    /* If it is a call to a service, add the call to the service, this is where we find the conversation id. */
    if (msg_inst->context.is_service_call) {
        memcpy(&msg_inst->service_id, data + curr_pos, 2);
        curr_pos += 2;
    }
    /* If it is a conversation, this is where we find the conversation id. */
    if (msg_inst->context.is_conversation) {
        memcpy(&msg_inst->conversation_id, data + curr_pos, 2);
        curr_pos += 2;
    }

    /* If there are null terminated string arrays in the message, they begin here */ 
    if (msg_inst->context.has_strings) {
        uint16_t strings_end = 0;
        if (msg_inst->context.has_binary) {
            // If it also has a binary part, it must say how long the strings are, and we can infer where they end.
            strings_end = (uint16_t)(data[curr_pos]) + curr_pos + 2;
            curr_pos += 2;
        } else {
            strings_end = data_len;
        }
        // Loop to count all null values.
        int nullcount = 0;
        for (int i = curr_pos; i < strings_end; i++)
        {
            if (data[i] == 0)
            {
                nullcount++;
            }
        }
        if (nullcount == 0) {
            // TODO: Should we add separate error codes for security violations? 
            // TODO: And a ROB_EXT_LOGE()-macro that sends to a logging (forwarding    ) server? Or are we doing categories as well for that?
            ROB_LOGE(message_parsing_log_prefix, "The strings had no null separators at all, this is a protocol and security violation. Message discarded.");
            // TODO: Mark the connection suspect
            rob_log_bit_mesh(ROB_LOG_ERROR, message_parsing_log_prefix, data, data_len);
            return ROB_ERR_PARSING_FAILED;            
        }
        if (data[strings_end -1] != 0) {
            ROB_LOGE(message_parsing_log_prefix, "The strings section has not ended with a null value, this is a protocol and security violation. Message discarded.");
            rob_log_bit_mesh(ROB_LOG_ERROR, message_parsing_log_prefix, data, data_len);
            return ROB_ERR_PARSING_FAILED;
        }
        ROB_LOGD(message_parsing_log_prefix, "Number of null terminators: %d", nullcount);
        msg_inst->strings = robusto_malloc(nullcount * sizeof(int32_t)); 
        // The first byte is always the beginning of a part
        msg_inst->strings[0] = (char *)(data + curr_pos);
        msg_inst->string_count = 0;
        ROB_LOGD(message_parsing_log_prefix, "String[%i] = %s", msg_inst->string_count, msg_inst->strings[msg_inst->string_count]);
        // Loop the data and set pointers, note that it disregards it if it doesn't end with a NULL value.
        for (uint32_t j = curr_pos; j < strings_end; j++)
        {
            if (data[j] == 0)
            {
                msg_inst->string_count++;
                // The last null does not mean there is more data.
                if (j < strings_end -1) {
                    msg_inst->strings[msg_inst->string_count] = (char *)(data + j + 1);
                    ROB_LOGD(message_parsing_log_prefix, "String[%i] = %s", msg_inst->string_count, msg_inst->strings[msg_inst->string_count]);
                }
            }
        }
        curr_pos = strings_end;
    } else {
        msg_inst->strings = NULL;
        msg_inst->string_count = 0;

    }
    /* The rest of the message contains binary data */
    if (msg_inst->context.has_binary) {
        // There is a binary part,lets parse it
        if (data_len < curr_pos + 1) {
            msg_inst->binary_data_length = 0;
            ROB_LOGE(message_parsing_log_prefix, "The message seems has the binary bit set but no trailing binary data. ");
            rob_log_bit_mesh(ROB_LOG_ERROR, message_parsing_log_prefix, data, data_len);
            return ROB_ERR_PARSING_FAILED;
        } else {
            msg_inst->binary_data = &(data[curr_pos]);
            msg_inst->binary_data_length = data_len - curr_pos;
        }
    } else if (curr_pos < data_len -1)
    {
        /* As the message has gotten here, it has a gotten through the checks, which makes it a protocol error */
        ROB_LOGE(message_parsing_log_prefix, "The message seems to be have trailing data and no binary bit set. ");
        rob_log_bit_mesh(ROB_LOG_ERROR, message_parsing_log_prefix, data, data_len > 20 ? 20: data_len);
        return ROB_ERR_PARSING_FAILED;
    } else {
        msg_inst->binary_data = NULL;
        msg_inst->binary_data_length = 0;
    }
  

    ROB_LOGD(message_parsing_log_prefix, "Message successfully parsed.");
    return ROB_OK;

}

void robusto_message_parsing_init(char * _log_prefix)
{
    message_parsing_log_prefix = _log_prefix;
}