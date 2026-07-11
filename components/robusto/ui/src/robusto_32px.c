#include "robusto_32px.h"
#ifdef CONFIG_ROBUSTO_UI
#include <stdlib.h>
#include <string.h>

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    uint8_t dev_addr;
    uint8_t s_chDisplayBuffer[128][8];
} ssd1306_dev_t;

typedef void *ssd1306_handle_t;                         /*handle of ssd1306*/

#define SSD1306_WRITE_CMD           (0x00)
#define SSD1306_WRITE_DAT           (0x40)



static esp_err_t ssd1306_write_cmd(ssd1306_handle_t dev, const uint8_t *const data, const uint16_t data_len)
{
    ssd1306_dev_t *device = (ssd1306_dev_t *) dev;
    uint8_t control_byte = SSD1306_WRITE_CMD;
    i2c_operation_job_t operations[] = {
        {.command = I2C_MASTER_CMD_START},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = &device->dev_addr, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = &control_byte, .total_bytes = 1}},
        {.command = I2C_MASTER_CMD_WRITE, .write = {.ack_check = true, .data = data, .total_bytes = data_len}},
        {.command = I2C_MASTER_CMD_STOP},
    };
    return i2c_master_execute_defined_operations(device->device, operations, sizeof(operations) / sizeof(i2c_operation_job_t), 1000);
}

static inline esp_err_t ssd1306_write_cmd_byte(ssd1306_handle_t dev, const uint8_t cmd)
{
    return ssd1306_write_cmd(dev, &cmd, 1);
}

void ssd1306__init(ssd1306_handle_t dev)
{
    

    ssd1306_write_cmd_byte(dev, 0xAE); //--turn off oled panel
    ssd1306_write_cmd_byte(dev, 0xA8); //--set multiplex ratio(1 to 64)
    ssd1306_write_cmd_byte(dev, 0x1f); //--1/64 duty
    ssd1306_write_cmd_byte(dev, 0xDA); //--set com pins hardware configuration
    ssd1306_write_cmd_byte(dev, 0x02); //--set vcomh


    ssd1306_write_cmd_byte(dev, 0xAF); //--turn on oled panel

}



void ssd1306_init(i2c_port_num_t bus, uint16_t dev_addr)
{
    ssd1306_dev_t *dev = (ssd1306_dev_t *) calloc(1, sizeof(ssd1306_dev_t));
    if (i2c_master_get_bus_handle(bus, &dev->bus) != ESP_OK) {
        free(dev);
        return;
    }
    dev->dev_addr = dev_addr << 1;

    i2c_device_config_t device_config = {0};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
    device_config.scl_speed_hz = CONFIG_ROBUSTO_UI_I2C_FREQ_HZ;
    if (i2c_master_bus_add_device(dev->bus, &device_config, &dev->device) != ESP_OK) {
        free(dev);
        return;
    }
    ssd1306__init((ssd1306_handle_t) dev);

}

#endif