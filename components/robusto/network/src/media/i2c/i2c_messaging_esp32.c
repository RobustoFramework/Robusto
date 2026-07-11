/**
 * @file i2c_messaging_esp32.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP32 I2C implementation
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
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
#include <freertos/queue.h>
#include <driver/i2c_master.h>
#include <driver/i2c_slave.h>

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

typedef enum robusto_i2c_driver_mode
{
    ROBUSTO_I2C_MODE_NONE,
    ROBUSTO_I2C_MODE_MASTER,
    ROBUSTO_I2C_MODE_SLAVE,
} robusto_i2c_driver_mode_t;

typedef struct robusto_i2c_rx_item
{
    uint32_t data_length;
    uint8_t data[I2C_RX_BUF];
} robusto_i2c_rx_item_t;

static i2c_master_bus_handle_t i2c_master_bus;
static i2c_master_dev_handle_t i2c_master_device;
static i2c_slave_dev_handle_t i2c_slave_device;
static QueueHandle_t i2c_slave_rx_queue;
static robusto_i2c_driver_mode_t i2c_driver_mode = ROBUSTO_I2C_MODE_NONE;


static bool i2c_slave_receive_cb(i2c_slave_dev_handle_t i2c_slave, const i2c_slave_rx_done_event_data_t *evt_data, void *arg)
{
    (void)i2c_slave;
    QueueHandle_t rx_queue = (QueueHandle_t)arg;
    robusto_i2c_rx_item_t rx_item = {0};
    BaseType_t higher_priority_task_woken = pdFALSE;

    rx_item.data_length = evt_data->length;
    if (rx_item.data_length > I2C_RX_BUF)
    {
        rx_item.data_length = I2C_RX_BUF;
    }
    memcpy(rx_item.data, evt_data->buffer, rx_item.data_length);
    xQueueSendFromISR(rx_queue, &rx_item, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t i2c_delete_current_driver(void)
{
    esp_err_t first_error = ESP_OK;
    if (i2c_master_device != NULL)
    {
        esp_err_t ret = i2c_master_bus_rm_device(i2c_master_device);
        if (ret != ESP_OK && first_error == ESP_OK)
        {
            first_error = ret;
        }
        i2c_master_device = NULL;
    }
    if (i2c_master_bus != NULL)
    {
        esp_err_t ret = i2c_del_master_bus(i2c_master_bus);
        if (ret != ESP_OK && first_error == ESP_OK)
        {
            first_error = ret;
        }
        i2c_master_bus = NULL;
    }
    if (i2c_slave_device != NULL)
    {
        esp_err_t ret = i2c_del_slave_device(i2c_slave_device);
        if (ret != ESP_OK && first_error == ESP_OK)
        {
            first_error = ret;
        }
        i2c_slave_device = NULL;
    }
    i2c_driver_mode = ROBUSTO_I2C_MODE_NONE;
    return first_error;
}

static rob_ret_val_t i2c_install_master_driver(void)
{
    i2c_master_bus_config_t bus_config = {0};
    bus_config.i2c_port = (i2c_port_num_t)CONFIG_I2C_CONTROLLER_NUM;
    bus_config.sda_io_num = (gpio_num_t)CONFIG_I2C_SDA_IO;
    bus_config.scl_io_num = (gpio_num_t)CONFIG_I2C_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;

    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_master_bus);
    if (ret != ESP_OK)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Master - Failed creating bus. Code: %i", ret);
        return ROB_ERR_INIT_FAIL;
    }

    i2c_device_config_t device_config = {0};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
    device_config.scl_speed_hz = CONFIG_I2C_MAX_FREQ_HZ;

    ret = i2c_master_bus_add_device(i2c_master_bus, &device_config, &i2c_master_device);
    if (ret != ESP_OK)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Master - Failed adding device. Code: %i", ret);
        i2c_delete_current_driver();
        return ROB_ERR_INIT_FAIL;
    }
    i2c_driver_mode = ROBUSTO_I2C_MODE_MASTER;
    return ROB_OK;
}

static rob_ret_val_t i2c_install_slave_driver(void)
{
    if (i2c_slave_rx_queue == NULL)
    {
        i2c_slave_rx_queue = xQueueCreate(4, sizeof(robusto_i2c_rx_item_t));
        if (i2c_slave_rx_queue == NULL)
        {
            ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - Failed creating RX queue.");
            return ROB_ERR_OUT_OF_MEMORY;
        }
    }
    else
    {
        xQueueReset(i2c_slave_rx_queue);
    }

    i2c_slave_config_t slave_config = {0};
    slave_config.i2c_port = (i2c_port_num_t)CONFIG_I2C_CONTROLLER_NUM;
    slave_config.sda_io_num = (gpio_num_t)CONFIG_I2C_SDA_IO;
    slave_config.scl_io_num = (gpio_num_t)CONFIG_I2C_SCL_IO;
    slave_config.clk_source = I2C_CLK_SRC_DEFAULT;
    slave_config.send_buf_depth = I2C_TX_BUF;
    slave_config.receive_buf_depth = I2C_RX_BUF;
    slave_config.slave_addr = CONFIG_I2C_ADDR;
    slave_config.addr_bit_len = I2C_ADDR_BIT_LEN_7;

    esp_err_t ret = i2c_new_slave_device(&slave_config, &i2c_slave_device);
    if (ret != ESP_OK)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - Failed creating slave device. Code: %i", ret);
        return ROB_ERR_INIT_FAIL;
    }

    i2c_slave_event_callbacks_t callbacks = {0};
    callbacks.on_receive = i2c_slave_receive_cb;
    ret = i2c_slave_register_event_callbacks(i2c_slave_device, &callbacks, i2c_slave_rx_queue);
    if (ret != ESP_OK)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - Failed registering callbacks. Code: %i", ret);
        i2c_delete_current_driver();
        return ROB_ERR_INIT_FAIL;
    }
    i2c_driver_mode = ROBUSTO_I2C_MODE_SLAVE;
    return ROB_OK;
}

static esp_err_t i2c_master_run_ops(i2c_operation_job_t *operations, size_t operation_count, int timeout_ms)
{
    if (i2c_master_device == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_execute_defined_operations(i2c_master_device, operations, operation_count, timeout_ms);
}


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

    robusto_i2c_driver_mode_t requested_mode = is_master ? ROBUSTO_I2C_MODE_MASTER : ROBUSTO_I2C_MODE_SLAVE;
    if (i2c_driver_mode == requested_mode)
    {
        return ROB_OK;
    }

    if (!dont_delete)
    {
        esp_err_t delete_ret = i2c_delete_current_driver();
        if (delete_ret != ESP_OK)
        {
            ROB_LOGE(i2c_esp32_messaging_log_prefix, ">> Deleting driver failed. Code: %i", delete_ret);
            return ROB_ERR_INVALID_ARG;
        }
    }

    if (is_master)
    {
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - Installing driver");
        return i2c_install_master_driver();
    }

    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - Installing driver");
    return i2c_install_slave_driver();
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

        return ROB_ERR_SEND_FAIL;
    }
}
rob_ret_val_t i2c_after_comms(bool first_param, bool second_param)
{
    (void)i2c_set_master(false, false);
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
    while(1)
    {
        ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C Master - << Reading receipt from %hhu, try %i.", peer->i2c_address, read_retries);
        uint8_t read_address = (peer->i2c_address << 1) | 1;
        i2c_operation_job_t read_operations[] = {
            {.command = I2C_MASTER_CMD_START},
            {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = &read_address, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_READ, .read = {.ack_value = I2C_ACK_VAL, .data = dest_data, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_READ, .read = {.ack_value = I2C_NACK_VAL, .data = dest_data + 1, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_STOP},
        };
        read_ret = i2c_master_run_ops(read_operations, sizeof(read_operations) / sizeof(i2c_operation_job_t), 100);
    
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
    uint8_t write_address = peer->i2c_address << 1;
    uint8_t source_address = CONFIG_I2C_ADDR;
    i2c_operation_job_t write_operations[] = {
        {.command = I2C_MASTER_CMD_START},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = &write_address, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = &source_address, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = offset_data, .total_bytes = offset_data_length}},
        {.command = I2C_MASTER_CMD_STOP},
    };

    esp_err_t send_ret = ROB_FAIL;

    ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Master - >> Sending.");
    send_ret = i2c_master_run_ops(write_operations, sizeof(write_operations) / sizeof(i2c_operation_job_t), 1000);

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
    int ret_val = 0;
    prefix_bytes[0] = 0x01;
    int retries = 0;
    int data_len = 0;
    uint8_t *data = *rcv_data;
    bool is_heartbeat = false;

    data = robusto_malloc(I2C_RX_BUF);
    do
    {
        if (retries > 0)
        {
            robusto_yield();
        }
        do
        {
            robusto_i2c_rx_item_t rx_item = {0};
            if (i2c_slave_rx_queue == NULL)
            {
                robusto_free(data);
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - << RX queue is not initialized.");
                return ROB_FAIL;
            }
            ret_val = xQueueReceive(i2c_slave_rx_queue, &rx_item, 10 / portTICK_PERIOD_MS) == pdTRUE ? rx_item.data_length : 0;
            if (ret_val > 0 && data_len + ret_val <= I2C_RX_BUF)
            {
                memcpy(data + data_len, rx_item.data, ret_val);
                ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - We got %i bytes of data.", ret_val);
                data_len = data_len + ret_val;
            }
            else if (ret_val > 0)
            {
                ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - << Got too much data: %i bytes", data_len + ret_val);
                ret_val = ROB_ERR_MESSAGE_TOO_LONG;
            }
            else
            {
                ret_val = 0;
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

    uint32_t write_len = 0;
    esp_err_t ret = i2c_slave_device == NULL ? ESP_ERR_INVALID_STATE : i2c_slave_write(i2c_slave_device, (uint8_t *)&receipt, 2, &write_len, I2C_TIMEOUT_MS);
    if (ret != ESP_OK || write_len != 2)
    {
        ROB_LOGE(i2c_esp32_messaging_log_prefix, "I2C Slave - >> Got an error from sending back data: %i", ret);
        if (peer)
        {
            peer->i2c_info.send_failures++; // TODO: Not sure how this and the other below should count, but probably it should.
        }
    }
    else
    {
        ROB_LOGD(i2c_esp32_messaging_log_prefix, "I2C Slave - >> Sent %" PRIu32 " bytes: %hhx%hhx", write_len, receipt[0], receipt[1]);
        if (peer)
        {
            peer->i2c_info.send_successes++;
        }
    }
    return ret == ESP_OK && write_len == 2 ? ROB_OK : ROB_FAIL;
}

void i2c_compat_messaging_start() {

    i2c_set_master(false, true);
}


void i2c_compat_messaging_init(char *_log_prefix)
{
    // TODO: Implement detection and handling of these error states: https://arduino.stackexchange.com/questions/46680/i2c-packet-ocasionally-send-a-garbage-data
    i2c_esp32_messaging_log_prefix = _log_prefix;
}

#endif