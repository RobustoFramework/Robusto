/**
 * @file robusto_message_sending.c
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Robusto functionality for receiving messages
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
#include <robusto_logging.h>
#include <robusto_retval.h>
#include <inttypes.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <src/media/i2c/i2c_messaging.h>
#include <src/media/espnow/espnow_messaging.h>
#include <src/media/lora/lora_messaging.hpp>
#include <src/media/mock/mock_messaging.h>
#else
#include "../media/i2c/i2c_messaging.h"
#include "../media/mock/mock_messaging.h"
#endif

char *message_receiving_log_prefix;
// TODO: Either this should go or be explained. This vs incoming and if used in testing.
rob_ret_val_t robusto_receive_message_media_type(e_media_type media_type, robusto_message_t **dest_msg)
{
    robusto_media_types host_supported_media_types = get_host_supported_media_types();
    //ROB_LOGI(message_receiving_log_prefix, ">> robusto_receive_message_media_type called, media type: %hhu ", media_type);
    if (!(host_supported_media_types & media_type))
    {
        ROB_LOGE(message_receiving_log_prefix, ">> robusto_receive_message_media_type called, media type %hu not supported, available are %hu", media_type, host_supported_media_types);
        return -ROB_ERR_NOT_SUPPORTED;
    }
    uint8_t *data;
    int data_len = 0;

    robusto_peer_t *peer;
    uint8_t prefix_bytes = 0;

    // Check the correct media type for data
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE

#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    if (media_type ==  robusto_mt_espnow)
    {
        data_len = esp_now_read_data (&data, &peer, &prefix_bytes);
    }
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

    if (media_type ==  robusto_mt_lora)
    {
        data = robusto_malloc(256); // LoRa max buffer
        data_len = lora_read_data(&data, &peer, &prefix_bytes);
        // TODO: No sure if this should return negative NO_MESSAGE. 
        // Should the application be bothered by singular errors, shouldn't that happen first when they are unrecoverable or something?
        // On the other hand, this should not be directly called by it. Usually.
        if (data_len <= 0) {
            robusto_free(data);
        }
    }
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C

    if (media_type == robusto_mt_i2c)
    {
        // Allocate receive buffer
        data = robusto_malloc(I2C_RX_BUF);
        data_len = i2c_read_data(&data, &peer, &prefix_bytes);
        if (data_len == 0) {
            robusto_free(data);
        }

    }
#endif
#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING

    if (media_type == robusto_mt_mock)
    {
        
        data_len = mock_read_data(&data, &peer);
        prefix_bytes = ROBUSTO_PREFIX_BYTES;
    }
#endif
    if (data_len > 0)
    {
        int retval = ROB_FAIL;
        rob_log_bit_mesh(ROB_LOG_INFO, message_receiving_log_prefix, data, data_len);
        if (robusto_check_message(data, data_len, prefix_bytes)) {
            retval = robusto_network_parse_message(data, data_len, peer,dest_msg, prefix_bytes);
        }
        return retval;

    }
    else
    {

        return ROB_INFO_RECV_NO_MESSAGE;
    }
    // return ROB_INFO_RECV_NO_MESSAGE;
}

void robusto_message_receiving_init(char *_log_prefix)
{
    message_receiving_log_prefix = _log_prefix;
}
