#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#if defined(CONFIG_XPOWERS_CHIP_AXP192) || defined(CONFIG_XPOWERS_CHIP_AXP2102)
#include <stdio.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "driver/i2c_master.h"

static const char *TAG = "XPowersLib";


#define I2C_MASTER_NUM                  (i2c_port_t)CONFIG_I2C_MASTER_PORT_NUM
#define I2C_MASTER_FREQ_HZ              CONFIG_I2C_MASTER_FREQUENCY /*!< I2C master clock frequency */
#define I2C_MASTER_SDA_IO               (gpio_num_t)CONFIG_PMU_I2C_SDA
#define I2C_MASTER_SCL_IO               (gpio_num_t)CONFIG_PMU_I2C_SCL


#define I2C_MASTER_TIMEOUT_MS           1000

#define WRITE_BIT                       0
#define READ_BIT                        1
#define ACK_CHECK_EN                    true

static i2c_master_bus_handle_t pmu_i2c_bus;
static i2c_master_dev_handle_t pmu_i2c_device;

static esp_err_t pmu_i2c_init_bus(void)
{
    if (pmu_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_MASTER_NUM;
    bus_config.sda_io_num = I2C_MASTER_SDA_IO;
    bus_config.scl_io_num = I2C_MASTER_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus_config, &pmu_i2c_bus);
}

static esp_err_t pmu_i2c_get_device(i2c_master_dev_handle_t *device)
{
    esp_err_t ret = pmu_i2c_init_bus();
    if (ret != ESP_OK) {
        return ret;
    }
    if (pmu_i2c_device != NULL) {
        *device = pmu_i2c_device;
        return ESP_OK;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
    device_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    device_config.scl_wait_us = I2C_MASTER_TIMEOUT_MS * 1000;
    ret = i2c_master_bus_add_device(pmu_i2c_bus, &device_config, &pmu_i2c_device);
    if (ret == ESP_OK) {
        *device = pmu_i2c_device;
    }
    return ret;
}


/**
 * @brief Read a sequence of bytes from a pmu registers
 */
int pmu_register_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    if (data == NULL) {
        return ESP_FAIL;
    }
    i2c_master_dev_handle_t device = NULL;
    esp_err_t ret = pmu_i2c_get_device(&device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU i2c_master_cmd_begin FAILED! Error: %i" , ret);
        return ESP_FAIL;
    }
    uint8_t write_address = (devAddr << 1) | WRITE_BIT;
    i2c_operation_job_t set_register_ops[] = {
        {.command = I2C_MASTER_CMD_START, .write = {}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &write_address, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &regAddr, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_STOP, .write = {}},
    };
    ret = i2c_master_execute_defined_operations(device, set_register_ops, sizeof(set_register_ops) / sizeof(i2c_operation_job_t), I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU i2c_master_cmd_begin FAILED! Error: %i" , ret);
        return ESP_FAIL;
    }

    uint8_t read_address = (devAddr << 1) | READ_BIT;
    if (len > 1) {
        i2c_operation_job_t read_ops[] = {
            {.command = I2C_MASTER_CMD_START, .write = {}},
            {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &read_address, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_READ, .read = {.ack_value = I2C_ACK_VAL, .data = data, .total_bytes = (size_t)(len - 1)}},
            {.command = I2C_MASTER_CMD_READ, .read = {.ack_value = I2C_NACK_VAL, .data = data + len - 1, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_STOP, .write = {}},
        };
        ret = i2c_master_execute_defined_operations(device, read_ops, sizeof(read_ops) / sizeof(i2c_operation_job_t), I2C_MASTER_TIMEOUT_MS);
    } else {
        i2c_operation_job_t read_ops[] = {
            {.command = I2C_MASTER_CMD_START, .write = {}},
            {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &read_address, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_READ, .read = {.ack_value = I2C_NACK_VAL, .data = data, .total_bytes = 1}},
            {.command = I2C_MASTER_CMD_STOP, .write = {}},
        };
        ret = i2c_master_execute_defined_operations(device, read_ops, sizeof(read_ops) / sizeof(i2c_operation_job_t), I2C_MASTER_TIMEOUT_MS);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU READ FAILED! > ");
    }
    return ret == ESP_OK ? 0 : -1;
}

/**
 * @brief Write a byte to a pmu register
 */
int pmu_register_write_byte(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (data == NULL) {
        return ESP_FAIL;
    }
    i2c_master_dev_handle_t device = NULL;
    esp_err_t ret = pmu_i2c_get_device(&device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU failed to get I2C device. Error: %i" , ret);
        return ESP_FAIL;
    }
    uint8_t write_address = (devAddr << 1) | WRITE_BIT;
    i2c_operation_job_t write_ops[] = {
        {.command = I2C_MASTER_CMD_START, .write = {}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &write_address, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = &regAddr, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = ACK_CHECK_EN, .data = data, .total_bytes = len}},
        {.command = I2C_MASTER_CMD_STOP, .write = {}},
    };
    ret = i2c_master_execute_defined_operations(device, write_ops, sizeof(write_ops) / sizeof(i2c_operation_job_t), I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU WRITE FAILED! < ");
    }
    return ret == ESP_OK ? 0 : -1;
}


/**
 * @brief i2c master initialization
 */
esp_err_t i2c_init(void)
{
    return pmu_i2c_init_bus();
}

#endif
#endif