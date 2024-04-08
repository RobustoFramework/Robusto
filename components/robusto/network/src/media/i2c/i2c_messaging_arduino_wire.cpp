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
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include <inttypes.h>
#include <string.h>

#define I2C_TIMING 0x00B21847
static char *i2c_wire_messaging_log_prefix;
static bool ismaster = false;

static uint8_t incoming_data[I2C_RX_BUF];
static uint32_t incoming_data_length;
static uint8_t i2c_arduino_receipt[2];

rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt);

void i2c_incoming_cb(int howMany)
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "In wire i2c_incoming_cb. %i bytes", howMany);
    int data_length = Wire.available();
    if (data_length > I2C_RX_BUF)
    {
        ROB_LOGE(i2c_wire_messaging_log_prefix, "Error, too large message with %i bytes, cannot be larger than I2C_RX_BUF.", data_length);
        return;
    }
    incoming_data_length = data_length;
    Wire.readBytes((uint8_t *)&incoming_data, data_length); // receive byte as a character
}

void i2c_peripheral_request()
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "In wire i2c_peripheral_request - send receipt");
    if (i2c_arduino_receipt[0] + i2c_arduino_receipt[1]) {
        Wire.write((uint8_t *)&i2c_arduino_receipt, (size_t)2);
        i2c_arduino_receipt[0] = 0;
        i2c_arduino_receipt[1] = 0;
    } else {
        ROB_LOGW(i2c_wire_messaging_log_prefix, "In wire i2c_peripheral_request - have no data to send");
    }
    
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
        ROB_LOGI(i2c_wire_messaging_log_prefix, "Wire I2C set to slave");
    }
    ismaster = is_master;
    return ROB_OK;
}

rob_ret_val_t i2c_before_comms(bool is_sending, bool first_call)
{

    // TODO: Think that this isn't needed?
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

rob_ret_val_t i2c_read_receipt(robusto_peer_t *peer)
{
    uint8_t *dest_data = (uint8_t *)robusto_malloc(20);
    // aa everywhere makes it easy to discern from random noise
    memset(dest_data, 0xaa, 20);
    int read_retries = 0;
    int read_ret = ROB_FAIL;

    r_delay(10);
    do
    {
        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - << Reading receipt from %u, try %i.", peer->i2c_address, read_retries);
        Wire.requestFrom(peer->i2c_address, 2);
        if (Wire.available())
        {
            Wire.readBytes(dest_data, 2);
            read_ret = ROB_OK;
        }
        else
        {
            read_ret = ROB_FAIL;
        }

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
                if (!(dest_data[0] == 0xaa || dest_data[1] == 0xaa)) {
                    rob_log_bit_mesh(ROB_LOG_INFO, i2c_wire_messaging_log_prefix, dest_data, 20);
                }                
                r_delay(CONFIG_ROB_RECEIPT_TIMEOUT_MS);
                read_ret = ROB_FAIL;
            }
        }
        read_retries++;
    } while ((read_ret != ROB_OK) && (read_retries < 3)); // We always retry reading three times. TODO: Why?

    if ((read_ret == ROB_OK) && (read_retries < 3))
    {
        // TODO: Break out from both wire and esp32
        if ((dest_data[0] == 0x0f) && (dest_data[1] == 0xf0))
        {
            read_ret = ROB_ERR_WHO;
            peer->state = PEER_UNKNOWN;
            ROB_LOGW(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt: Rejected, the peer has forgotten us.");
        }
        else if ((dest_data[0] == 0xff) && (dest_data[1] == 0x00))
        {
            read_ret = ROB_OK;
            peer->i2c_info.send_successes++;
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt: OK.");
        }
        else
        {
            read_ret = ROB_FAIL;
            peer->i2c_info.send_failures++;

            ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Master - << Read receipt: FAILED, %i retries:", read_retries - 1);
            rob_log_bit_mesh(ROB_LOG_ERROR, i2c_wire_messaging_log_prefix, dest_data, 20);
        }
    }
    robusto_free(dest_data);

    return read_ret;
}

rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
    int64_t starttime = r_millis();
    int send_retries = 0;
    rob_ret_val_t send_ret = ROB_FAIL;
    i2c_set_master(true, false);
    data_length = data_length - ROBUSTO_PREFIX_BYTES;
    uint8_t *msg = (uint8_t *)robusto_malloc(data_length + 1);
    memcpy(msg + 1, data + ROBUSTO_PREFIX_BYTES, data_length);
    msg[0] = CONFIG_I2C_ADDR;
    do
    {

        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - >> Sending %i bytes to %hu, try %i.", data_length, peer->i2c_address, send_retries + 1);

        Wire.beginTransmission(peer->i2c_address);
        send_ret = Wire.write(msg, data_length + 1);
        if (send_ret == 0)
        {
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Master - >> Send failure");
        }
        Wire.endTransmission();
        send_ret = ROB_OK;

        if (send_ret != ROB_OK)
        {

            ROB_LOGE(i2c_wire_messaging_log_prefix, "I2C Master - >> Send failure, code %i.", send_ret);

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

    if (receipt)
    {
        send_ret = i2c_read_receipt(peer);
    }

    i2c_set_master(false, false);
    return send_ret;
}

rob_ret_val_t i2c_send_receipt(robusto_peer_t *peer, bool success, bool unknown)
{
    ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> In i2c_send_receipt");

    if (unknown)
    {
        i2c_arduino_receipt[0] = 0x0f;
        i2c_arduino_receipt[1] = 0xf0;
    }
    else if (success)
    {
        if (peer)
        {
            peer->i2c_info.send_successes++;
        }
        i2c_arduino_receipt[0] = 0xff;
        i2c_arduino_receipt[1] = 0x00;
    }
    else
    {
        i2c_arduino_receipt[0] = 0x00;
        i2c_arduino_receipt[1] = 0xff;
        if (peer)
        {
            peer->i2c_info.last_send = r_millis();
            peer->i2c_info.send_failures++; // TODO: Not sure how this should count, but probably it should
        }

    }
    
    return ROB_OK;
}

int i2c_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes)
{

    int ret_val;
    prefix_bytes[0] = 0x01;
    int retries = 0;
    int data_length = 0;
    uint8_t *data;
    do
    {
        if (retries > 0)
        {
            r_delay(1);
        }
        if (incoming_data_length > 0)
        {
            (*peer)->i2c_info.last_receive = r_millis();
            ret_val = ROB_OK;
            data = incoming_data;
            data_length = incoming_data_length;
            incoming_data_length = 0;
        }
        else
        {
            ret_val = ROB_FAIL;
        }
        // TODO: Will we get shorter writes?
        retries++;
    } while ((data_length == 0) && (retries < 3)); // Continue as long as there is a result.
    // If we haven't got any data, and an error in the end, we return that error.
    if (data_length == 0 && ret_val != 0)
    {
        return ROB_FAIL;
    }
    else
    {
        // TODO: Move this as well to i2c_handle_incoming.
        ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Checking message");
        // Check CRC
        if (robusto_check_message(data, data_length, 1))
        {
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Message valid, sending receipt.");
            i2c_send_receipt(*peer, true, false);
            
            i2c_handle_incoming(data, data_length);
            return data_length;
        }
        else
        {
            ROB_LOGI(i2c_wire_messaging_log_prefix, "I2C Slave - >> Message NOT valid, sending invalid receipt.");
            i2c_send_receipt(*peer, false, false);
            return ROB_FAIL;
        }
    }
}

void i2c_compat_messaging_start()
{
}

void i2c_compat_messaging_init(char *_log_prefix)
{
    // TODO: Implement detection and handling of these error states: https://arduino.stackexchange.com/questions/46680/i2c-packet-ocasionally-send-a-garbage-data
    i2c_wire_messaging_log_prefix = _log_prefix;
    incoming_data_length = 0;
    i2c_arduino_receipt[0] = 0;
    i2c_arduino_receipt[1] = 0;
    
    Wire.setSCL(CONFIG_I2C_SCL_IO);
    Wire.setSDA(CONFIG_I2C_SDA_IO);

    Wire.onReceive(&i2c_incoming_cb);
    Wire.onRequest(&i2c_peripheral_request);
    i2c_set_master(false, true);
}

#endif