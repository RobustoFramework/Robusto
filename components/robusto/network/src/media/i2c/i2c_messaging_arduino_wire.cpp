/**
 * @file i2c_messaging_arduino.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Arduino I2C implementation
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

#include "i2c_messaging.h"

#if defined(ARDUINO_ARCH_RP2040) && defined(CONFIG_ROBUSTO_SUPPORTS_I2C)

#include <Wire.h>

#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include <robusto_peer.h>
#include <inttypes.h>
#include <string.h>

#define I2C_TIMING 0x00B21847
static char *i2c_wire_messaging_log_prefix;
static bool ismaster = true; // TODO: Defaulting this to true is not the nicest workaround.
static uint8_t i2c_arduino_data[I2C_RX_BUF];
static uint32_t i2c_arduino_data_length = 0;
static uint8_t i2c_arduino_receipt[2];

void receiveEvent(int howMany)
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "In wire receiveEvent");
    int count = Wire.available();
    Wire.readBytes((uint8_t *)&i2c_arduino_data, count); // receive byte as a character
    i2c_arduino_data_length = count;
    rob_log_bit_mesh(ROB_LOG_INFO, i2c_wire_messaging_log_prefix, (uint8_t *)&i2c_arduino_data, count);
}

void requestEvent()
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "In wire requestEvent");
    Wire.write(i2c_arduino_receipt, 2);
    // TODO: This is just an entirely *awful* "solution" to peripheral writing. Can't we either use another library or other way?
    i2c_arduino_receipt[0] = 0;
    i2c_arduino_receipt[1] = 0;
}

rob_ret_val_t i2c_set_master(bool is_master, bool dont_delete)
{

    if (ismaster == is_master)
    {
        return ROB_OK;
    }

    if (!dont_delete)
    {

        Wire.end();
    }

    if (is_master)
    {

        Wire.setClock(CONFIG_I2C_MAX_FREQ_HZ);
        Wire.begin();

        ROB_LOGI(i2c_wire_messaging_log_prefix, "Wire I2C set to master");
    }
    else
    {

        Wire.begin(CONFIG_I2C_ADDR);
        Wire.onReceive(receiveEvent);
        Wire.onRequest(requestEvent);

        ROB_LOGI(i2c_wire_messaging_log_prefix, "Wire I2C set to slave");
    }
    ismaster = is_master;

    return ROB_OK;
}

rob_ret_val_t i2c_before_comms(bool is_sending, bool first_call)
{
    if (is_sending)
    {

        if (robusto_gpio_get_level(CONFIG_I2C_SDA_IO) == 1)
        {

            ROB_LOGI(i2c_wire_messaging_log_prefix, "Wire I2C Master - >> SDA was high.");

            return i2c_set_master(is_sending, false);
        }
        else
        {
            ROB_LOGW(i2c_wire_messaging_log_prefix, "I2C Master - >> SDA was low, seems we have to listen first..");
            i2c_set_master(false, false);

            return -ROB_ERR_SEND_FAIL;
        }
    }
    else
    {
        // Why is this called when not sending?
        return -ROB_FAIL;
    }
}
rob_ret_val_t i2c_after_comms(bool first_param, bool second_param)
{

    int ret = i2c_set_master(false, false);

    return ret;
}

rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, int data_length, bool receipt)
{
    int64_t starttime = r_millis();
    int send_retries = 0;
    rob_ret_val_t send_ret = ROB_FAIL;
    uint8_t *msg = (uint8_t *)robusto_malloc(data_length + 1);
    memcpy(msg + 1, data, data_length);
    msg[0] = CONFIG_I2C_ADDR;

    do
    {

        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - >> Sending, try %i.", send_retries + 1);

        Wire.beginTransmission(peer->i2c_address);
        Wire.write(CONFIG_I2C_ADDR);
        Wire.write(data, data_length);

        Wire.endTransmission();
        send_ret = ROB_OK;

        if (send_ret != ROB_OK)
        {

            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - >> Send failure, code %i.", send_ret);

            r_delay(I2C_TIMEOUT_MS / 4);
            // TODO: We need to follow up these small failures
        }
        else
        {
            peer->i2c_info.last_send = r_millis();
        }

        send_retries++;
    } while ((send_ret != ROB_OK) && (send_retries < 40));

    if (send_retries > 3)
    {
        // TODO: Add "just checking" hearbeat logging suppression here.
        ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Master - >> Failed to send after %i retries . Code: %i", send_retries, send_ret);
    }
    else
    {
        float speed = (float)(data_length / ((float)(r_millis() - starttime)) * 1000);
        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - >> %d byte packet sent...speed %f byte/s, air time: %lli , return value: %i",
                 data_length, speed, r_millis() - starttime, send_ret);
        peer->i2c_info.actual_speed = (peer->i2c_info.actual_speed + speed) / 2;
        peer->i2c_info.theoretical_speed = (CONFIG_I2C_MAX_FREQ_HZ / 2);
    }

    return send_ret;
}

rob_ret_val_t i2c_read_receipt(robusto_peer_t *peer)
{
    uint8_t *dest_data = (uint8_t *)robusto_malloc(20);
    memset(dest_data, 0xAA, 20);

    /*
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_start(cmd));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_byte(cmd, (peer->i2c_address << 1) | I2C_MASTER_READ, ACK_CHECK_EN));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_read(cmd, dest_data, 19, ACK_VAL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_read_byte(cmd, dest_data + 19, NACK_VAL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_stop(cmd));
*/
    int read_retries = 0;
    int read_ret = ROB_FAIL;

    // TODO: This should probably use CONFIG_ROBUSTO_RECEIPT_TIMEOUT_MS * 1000 in some way.
    // And SDP_RECEIPT must be recalculated based on transmission speeds.
    // Also, the delay here is slightly strange.

    r_delay(50);
    do
    {
        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - << Reading receipt from %u, try %i.", peer->i2c_address, read_retries);
        Wire.requestFrom(peer->i2c_address, 2);
        if (Wire.available())
        {
            Wire.readBytes(dest_data, 2);
        }
        else
        {
            return ROB_FAIL;
        }

        return ROB_FAIL;

        // read_ret = i2c_master_read_from_device(CONFIG_I2C_CONTROLLER_NUM, peer->i2c_address, dest_data, 20, 1000 / portTICK_PERIOD_MS);

        // TODO: It seems like we always get some unexpected bytes here. The weird thing is that is almost always the same value.
        if ((read_ret != ROB_OK) ||
            !(
                ((dest_data[0] == 0x00) && (dest_data[1] == 0xff)) ||
                ((dest_data[0] == 0xff) && (dest_data[1] == 0x00))))
        {

            if (read_retries > 2)
            {
                ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Master - >> Failed to read receipt after %i retries. Error code: %i.",
                         read_retries, read_ret);
            }
            else
            {
                ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt failure, code %i. Waiting a short while.", read_ret);
                rob_log_bit_mesh(ROB_LOG_INFO, i2c_wire_messaging_log_prefix, dest_data, 20);
                r_delay(CONFIG_ROB_RECEIPT_TIMEOUT_MS);
                read_ret = ROB_FAIL;
            }
        }
        read_retries++;
    } while ((read_ret != ROB_OK) && (read_retries < 3)); // We always retry reading three times. TODO: Why?

    if ((read_ret == ROB_OK) && (read_retries < 3))
    {
        peer->i2c_info.last_receive = r_millis();
        if ((dest_data[0] == 0xff) && (dest_data[1] == 0x00))
        {
            read_ret = ROB_OK;
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt, was OK.");
            // rob_log_bit_mesh(ROB_LOG_INFO, i2c_wire_messaging_log_prefix, dest_data, 20);
        }
        else
        {
            read_ret = ROB_FAIL;
            ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt, was FAILED, %i retries:", read_retries - 1);
            rob_log_bit_mesh(ROB_LOG_INFO, i2c_wire_messaging_log_prefix, dest_data, 20);
        }
    }
    robusto_free(dest_data);

    // i2c_cmd_link_delete(cmd);
    return read_ret;
}

int i2c_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes)
{
    int ret_val;
    prefix_bytes[0] = 0x01;
    int retries = 0;
    int data_len = 0;
    uint8_t *data = *rcv_data;
    do
    {
        if (retries > 0)
        {
            r_delay(1);
        }
        do
        {

            if (i2c_arduino_data_length > 0)
            {
                (*peer)->i2c_info.last_receive = r_millis();
                // memcpy((uint8_t *)data, &i2c_arduino_data, i2c_arduino_data_length);
                data_len = i2c_arduino_data_length;
                ret_val = ROB_OK;
                i2c_arduino_data_length = 0;
            }
            else
            {
                ret_val = ROB_FAIL;
                r_delay(50);
            }

            /*
            if (ret_val > I2C_RX_BUF)
            {
                // TODO: Add another buffer, chain them or something, obviously we need to be able to receive much more data
                ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Slave - << Got too much data: %i bytes", data_len);
            }

            if (ret_val> 0) {
                ROB_LOGE(i2c_wire_messaging_log_prefix, "We have %i bytes of data ", ret_val);
                ret_val = Wire.readBytes(data, I2C_RX_BUF);
            }
            */

            data_len = data_len + i2c_arduino_data_length;

        } while (i2c_arduino_data_length > 0); // Continue as long as there is a result.
        retries++;
    } while ((data_len == 0) && (retries < 3)); // Continue as long as there is a result.
    // If we haven't got any data, and an error in the end, we return that error.
    if (data_len == 0 && ret_val != 0)
    {
        return ROB_FAIL;
    }
    else
    {

        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Checking message");
        // Check CRC
        if (robusto_check_message(data, i2c_arduino_data_length, 1))
        {
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Message valid, sending receipt.");
            // TODO: Implement reading hearbeats and not responding
            //(*peer)->espnow_info.last_peer_receive = parse_heartbeat(data, I2C_ADDR_LEN + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
            //  TODO: Implement handling
            i2c_send_receipt(*peer, true, false);
            return data_len;
        }
        else
        {
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Message NOT valid, sending invalid receipt.");
            i2c_send_receipt(*peer, false, false);
            return ROB_FAIL;
        }
    }
}

rob_ret_val_t i2c_send_receipt(robusto_peer_t *peer, bool success, bool unknown)
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> In i2c_send_receipt");

    // char receipt[2];
    if (unknown)
    {
        i2c_arduino_receipt[0] = 0x0f;
        i2c_arduino_receipt[1] = 0xf0;
    }
    else if (success)
    {
        i2c_arduino_receipt[0] = 0xff;
        i2c_arduino_receipt[1] = 0x00;
    }
    else
    {
        i2c_arduino_receipt[0] = 0x00;
        i2c_arduino_receipt[1] = 0xff;
    }

// return ROB_OK;
#if defined(ARDUINO_ARCH_RP2040)
    int ret = Wire.write(i2c_arduino_receipt, 2);
    if (ret = 0)
    {
        ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Slave - >> Error: %u", ret);

#elif defined(ARDUINO_ARCH_STM32)

    // int ret = i2c_slave.write((char *)&i2c_arduino_receipt, 2);
    r_delay(1000);

    int ret = HAL_I2C_Slave_Transmit(&I2cHandle, (uint8_t *)&i2c_arduino_receipt, 2, 5000);

    if (ret != HAL_OK)
    {
        ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Slave - >> Result: %u, ErrorCode: %u, State: %u, Size: %u", ret, I2cHandle.ErrorCode, I2cHandle.State, I2cHandle.XferSize);

//  (i2c1, "\ff\00");
#endif

        if (peer)
        {
            peer->i2c_info.send_failures++; // TODO: Not sure how this should count, but probably it should
        }
        ret = ROB_FAIL;
    }
    else
    {
        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Sent %i bytes: %02x%02x", ret, i2c_arduino_receipt[0], i2c_arduino_receipt[1]);
        ret = ROB_OK;
        peer->i2c_info.last_send = r_millis();
        peer->i2c_info.send_successes++;
    }

    return ret;
}

void i2c_compat_messaging_start()
{
}

void i2c_compat_messaging_init(char *_log_prefix)
{
    // TODO: Implement detection and handling of these error states: https://arduino.stackexchange.com/questions/46680/i2c-packet-ocasionally-send-a-garbage-data
    i2c_wire_messaging_log_prefix = _log_prefix;
    i2c_arduino_receipt[0] = 0;
    i2c_arduino_receipt[1] = 0;

    i2c_set_master(false, true);
    /*
        i2c_slave.address(CONFIG_I2C_ADDR);
        i2c_slave.frequency(100000);
        i2c_slave.stop();
        */
}

#endif