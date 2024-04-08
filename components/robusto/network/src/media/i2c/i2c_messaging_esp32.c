/**
 * @file i2c_messaging_esp32.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP32 I2C implementation
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
#if defined(USE_ESPIDF) && defined(CONFIG_ROBUSTO_SUPPORTS_I2C)

#include <freertos/FreeRTOS.h>
#include <driver/i2c.h>

#include <robusto_logging.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_incoming.h>
#include <inttypes.h>
#include <string.h>
// int i2c_hearbeat(robusto_peer *peer);

static char *i2c_esp32_messaging_log_prefix;

#define I2C_ADDR_LEN 1


rob_ret_val_t i2c_set_master(bool is_master, bool dont_delete)
{
    if (is_master)
    {
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "Setting I2C driver to master mode.");
    }
    else
    {
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "Setting I2C driver to slave mode at address %x.", CONFIG_I2C_ADDR);
    }

    if (!dont_delete)
    {
        rob_ret_val_t delete_ret = ROB_FAIL;
        delete_ret = i2c_driver_delete(CONFIG_I2C_CONTROLLER_NUM);
        if (delete_ret == ROB_ERR_INVALID_ARG)
        {
            ROB_LOGE(i2c_esp32_messaging_log_prefix, ">> Deleting driver caused an invalid arg-error.");
            return ROB_ERR_INVALID_ARG;
        }
    }

    if (is_master)
    {

        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = CONFIG_I2C_SDA_IO,
            .scl_io_num = CONFIG_I2C_SCL_IO,
            .sda_pullup_en = GPIO_PULLUP_DISABLE,
            .scl_pullup_en = GPIO_PULLUP_DISABLE,
            .master.clk_speed = CONFIG_I2C_MAX_FREQ_HZ,
            .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
        };
        ESP_ERROR_CHECK(i2c_param_config(CONFIG_I2C_CONTROLLER_NUM, &conf));

        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - Installing driver");
        return i2c_driver_install(CONFIG_I2C_CONTROLLER_NUM, conf.mode, 0, 0, 0);
    }
    else
    {

        i2c_config_t conf = {
            .mode = I2C_MODE_SLAVE,
            .sda_io_num = CONFIG_I2C_SDA_IO,
            .scl_io_num = CONFIG_I2C_SCL_IO,
            .sda_pullup_en = GPIO_PULLUP_DISABLE,
            .scl_pullup_en = GPIO_PULLUP_DISABLE,
            //.master.clk_speed = I2C_FREQ_HZ,
            .slave.slave_addr = CONFIG_I2C_ADDR,
            .slave.addr_10bit_en = false,
            .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
        };
        ESP_ERROR_CHECK(i2c_param_config(CONFIG_I2C_CONTROLLER_NUM, &conf));
        i2c_set_timeout((i2c_port_t)CONFIG_I2C_CONTROLLER_NUM, 0x1f); 
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - Installing driver");

        return i2c_driver_install(CONFIG_I2C_CONTROLLER_NUM, conf.mode, I2C_TX_BUF, I2C_RX_BUF, 0);
    }
}


rob_ret_val_t i2c_before_comms(bool is_sending, bool first_call)
{

    // If sending set master

    rob_ret_val_t mst_ret = i2c_set_master(is_sending, first_call);

    if (mst_ret != ROB_OK) {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, ">> Error %i setting I2C master mode.", mst_ret);
        return ROB_FAIL;
    }
    if (robusto_gpio_get_level(CONFIG_I2C_SDA_IO) == 1)
    {
        robusto_gpio_set_level(CONFIG_I2C_SDA_IO, 0);
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - >> SDA was high, now set to low.");
        return ROB_OK;
    } else {

        ROB_LOGW(i2c_esp32_messaging_log_prefix, "I2C Master - >> SDA was low, seems we have to listen first..");
        i2c_set_master(false, false);

        return -ROB_ERR_SEND_FAIL;
    }
}
rob_ret_val_t i2c_after_comms(bool first_param, bool second_param)
{
    rob_ret_val_t mst_ret = i2c_set_master(false, false);
    int ret = ROB_OK;
    return ret; 
}

rob_ret_val_t i2c_read_receipt(robusto_peer_t *peer)
{
    uint8_t *dest_data = robusto_malloc(20);

    memset(dest_data, 0xAA, 20);

    int read_retries = 0;
    int read_ret = ROB_FAIL;    
    r_delay(50);
    i2c_set_timeout((i2c_port_t)CONFIG_I2C_CONTROLLER_NUM, 0x1f); 
    while(1)
    {
        ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C Master - << Reading receipt from %hhu, try %i.", peer->i2c_address, read_retries);
        read_ret = i2c_master_read_from_device(CONFIG_I2C_CONTROLLER_NUM, peer->i2c_address, dest_data, 2, 100 / portTICK_PERIOD_MS);
    
        // TODO: It seems like we always get some unexpected bytes here. The weird thing is that is almost always the same value.
        // If it was either not successful, or that the data didn't indicate whether it worked or not..
        if ((read_ret != ESP_OK) || 
            !(
                ((dest_data[0] == 0x00) && (dest_data[1] == 0xff)) ||
                ((dest_data[0] == 0xff) && (dest_data[1] == 0x00))||
                ((dest_data[0] == 0x0f) && (dest_data[1] == 0xf0))
            )
        )
        {
            read_retries++;
            if (read_retries > 2)
            {
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Master - >> Failed to read receipt after %i retries. Error code: %i.",
                         read_retries, read_ret);
                break;
            }
            else
            {
                ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C Master - << Read receipt failure, code %i. Waiting a short while.", read_ret);
                rob_log_bit_mesh(ROB_LOG_DEBUG, i2c_esp32_messaging_log_prefix, dest_data, 20);
                r_delay(CONFIG_ROB_RECEIPT_TIMEOUT_MS);
                read_ret = ROB_FAIL;
            }
        } else {
            read_ret = ROB_OK;
            break;
        }  
    } 
    
    if ((read_ret == ROB_OK) && (read_retries < 3)) {
        if ((dest_data[0] == 0x0f) && (dest_data[1] == 0xf0)) {
            read_ret = ROB_ERR_WHO;
            peer->state = PEER_UNKNOWN;
            ROB_LOGW(i2c_esp32_messaging_log_prefix, "I2C Master - << Read receipt: Rejected, the peer has forgotten us.");
        } else
        if ((dest_data[0] == 0xff) && (dest_data[1] == 0x00)) {
            read_ret = ROB_OK;
            peer->i2c_info.send_successes++;
            ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C Master - << Read receipt: OK.");
        } else {
            read_ret = ROB_FAIL;
            peer->i2c_info.send_failures++;
            
            ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Master - << Read receipt: FAILED, %i retries:", read_retries -1);
            rob_log_bit_mesh(ROB_LOG_ERROR, i2c_esp32_messaging_log_prefix, dest_data, 20);
        }
    }
    robusto_free(dest_data);
    
    //i2c_cmd_link_delete(cmd);
    return read_ret;
}
rob_ret_val_t i2c_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
    #if CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_I2C_KILL_SWITCH) == true)
    {
        ROB_LOGE("I2C", "I2C KILL SWITCH ON - Failing receiving receipt");
        r_delay(100);
        return ROB_FAIL;
    }
    #endif

    // Offset Robusto preamble buffer (note that we don't add a byte for the address, this is done in the sending later)
    uint8_t *offset_data = data + ROBUSTO_PREFIX_BYTES;
    int offset_data_length = data_length - ROBUSTO_PREFIX_BYTES;
    

    ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C Master - >> Sending to %hhu, name %s.", peer->i2c_address, peer->name);   
    int64_t starttime = r_millis();
    i2c_before_comms(true, false);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (peer->i2c_address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, CONFIG_I2C_ADDR, ACK_CHECK_EN);
    i2c_master_write(cmd, offset_data, offset_data_length, ACK_CHECK_EN);
    i2c_master_stop(cmd);

    esp_err_t send_ret = ROB_FAIL;

    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - >> Sending.");
    send_ret = i2c_master_cmd_begin(CONFIG_I2C_CONTROLLER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (send_ret != ESP_OK)
    {
        ROB_LOGW(i2c_esp32_messaging_log_prefix, "I2C Master - >> Send failure to %hhu, code %i.", peer->i2c_address, send_ret);
        r_delay(I2C_TIMEOUT_MS / 4);
        peer->i2c_info.last_send = r_millis();
        peer->i2c_info.send_failures++;
        i2c_after_comms(false, false);
        return ROB_FAIL;
    }
    
    //------------------------  
    float speed = (float)(offset_data_length / ((float)(r_millis() - starttime)) * 1000);
    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - >> %d byte packet sent...speed %f byte/s, air time: %lli , return value: %i", 
        offset_data_length, speed, r_millis() - starttime, send_ret);
    peer->i2c_info.actual_speed = (peer->i2c_info.actual_speed + speed) / 2;
    peer->i2c_info.theoretical_speed = (CONFIG_I2C_MAX_FREQ_HZ / 2);  
    //------------------------  
    
    if (receipt) {
        send_ret = i2c_read_receipt(peer);
    } else {
        peer->i2c_info.last_send = r_millis();
        send_ret = ROB_OK;
    }
    
    // TODO: Remove these generalized after/before comm
    i2c_after_comms(false, false);
    return send_ret;
}


int i2c_read_data (uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes)
{
    int ret_val;
    prefix_bytes[0] = 0x01;
    int retries = 0;
    int data_len = 0;
    uint8_t *data = *rcv_data;
    bool is_heartbeat = false;

    data = robusto_malloc(255);
    do
    {
        if (retries > 0)
        {
            robusto_yield();
        }
        do
        {
            ret_val = i2c_slave_read_buffer(CONFIG_I2C_CONTROLLER_NUM, data + data_len, I2C_RX_BUF - data_len,
                                            10 / portTICK_PERIOD_MS);
            if (ret_val < 0)
            {
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - << Error %i reading buffer", ret_val);
                r_delay(100);
            }
            else
            {
                if (ret_val > 0) {
                    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - We got %i bytes of data.", ret_val);
                }
                
                // We only add 0 or positive values.
                data_len = data_len + ret_val;
            }
            
            if (data_len > I2C_RX_BUF)
            {
                // TODO: Add another buffer, chain them or something, obviously we need to be able to receive much more data
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - << Got too much data: %i bytes", data_len);
            }
        } while (ret_val > 0); // Continue as long as there is a result.
        retries++;
    } while ((data_len == 0) && (retries < 3)); // Continue as long as there is a result.
    
    // If we haven't got any data, and an error in the end, we return that error. 
    if (data_len == 0 && ret_val < 0)
    {
        robusto_free(data);
        return ret_val;
    }
    else
    {
        bool is_ok = false;
        if (data_len > 0) {
            // Should probably reallocate data to data_len
            *peer = robusto_peers_find_peer_by_i2c_address(data[0]); 
            if (*peer == NULL) {
                if (data[5] & MSG_NETWORK) {
                    // It is likely a presentation, add peer and hope it is. TODO: This could be flooded. 
                    *peer = robusto_add_init_new_peer_i2c(NULL, data[0]);
                } else {
                    // In I2C, which is a wired connection, we actually trust the peer so much that we 
                    // Respond with a receipt that asks who it is, this is not done in wireless environments.
                    i2c_send_receipt(NULL, false, true);
                    robusto_free(data);
                    return ROB_ERR_WHO;
                }
               
            }

            ROB_LOGD(i2c_esp32_messaging_log_prefix, "Message");
            rob_log_bit_mesh(ROB_LOG_DEBUG, i2c_esp32_messaging_log_prefix, data, data_len);
            // TODO: Move this to a more central spot and re-use. 
            is_ok = robusto_check_message(data,data_len, 1);
            is_heartbeat = data[I2C_ADDR_LEN + ROBUSTO_CRC_LENGTH] == HEARTBEAT_CONTEXT;
            if (is_heartbeat && is_ok) {  
                ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C is heartbeat");
                (*peer)->i2c_info.last_peer_receive = parse_heartbeat(data, I2C_ADDR_LEN + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
                
            } else if (i2c_send_receipt(*peer, is_ok, false) != ROB_OK) {
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "failed sending receipt.");
            }
        } else {

            robusto_free(data);
            return 0;
        } 
        
        if (is_ok) {
            (*peer)->i2c_info.last_receive = r_millis();
            
            if (is_heartbeat) {
                add_to_history(&((*peer)->i2c_info), false, ROB_OK);
                robusto_free(data);
                return 0;
            } else {
                add_to_history(&((*peer)->i2c_info), false, robusto_handle_incoming(data, data_len, *peer, robusto_mt_i2c, I2C_ADDR_LEN));
                return data_len;
            }
            
        } else {
            ROB_LOGW(i2c_esp32_messaging_log_prefix, "A message failed the check.");
            robusto_free(data);
            return 0;
        }
        
    }
}

rob_ret_val_t i2c_send_receipt(robusto_peer_t *peer, bool success, bool unknown)
{
    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - >> In i2c_send_receipt");
    char receipt[2];
    if (unknown) {
        receipt[0] = 0x0f;
        receipt[1] = 0xf0;
    } else
    if (success) {
        receipt[0] = 0xff;
        receipt[1] = 0x00;
    } else {
        receipt[0] = 0x00;
        receipt[1] = 0xff;
    }

    int ret = i2c_slave_write_buffer(CONFIG_I2C_CONTROLLER_NUM, (uint8_t *)&receipt, 2,
                                     I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret < 0)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - >> Got an error from sending back data: %i", ret);
        if (peer)
        {
            peer->i2c_info.send_failures++; // TODO: Not sure how this and the other below should count, but probably it should.
        }
    }
    else
    {
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - >> Sent %i bytes: %hhx%hhx", ret, receipt[0], receipt[1]);
        ret = 0;
        if (peer)
        {
            peer->i2c_info.send_successes++;
        }
    }
    return ret;
}

void i2c_compat_messaging_start() {

    i2c_set_master(false, true);
}


void i2c_compat_messaging_init(char *_log_prefix)
{
    // TODO: Implement detection and handling of these error states: https://arduino.stackexchange.com/questions/46680/i2c-packet-ocasionally-send-a-garbage-data
    i2c_esp32_messaging_log_prefix = _log_prefix;
    // This we have to set manually for some non-s3 cards 
    i2c_set_timeout((i2c_port_t)CONFIG_I2C_CONTROLLER_NUM, 1000 / portTICK_PERIOD_MS); 
}

#endif