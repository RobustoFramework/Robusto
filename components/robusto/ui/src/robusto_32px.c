#include "robusto_32px.h"
#ifdef CONFIG_ROBUSTO_UI
#include <driver/i2c.h>
#include <string.h>

typedef struct {
    i2c_port_t bus;
    uint16_t dev_addr;
    uint8_t s_chDisplayBuffer[128][8];
} ssd1306_dev_t;

typedef void *ssd1306_handle_t;                         /*handle of ssd1306*/

#define SSD1306_WRITE_CMD           (0x00)
#define SSD1306_WRITE_DAT           (0x40)



static esp_err_t ssd1306_write_cmd(ssd1306_handle_t dev, const uint8_t *const data, const uint16_t data_len)
{
    ssd1306_dev_t *device = (ssd1306_dev_t *) dev;
    esp_err_t ret;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret = i2c_master_start(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, device->dev_addr | I2C_MASTER_WRITE, true);
    assert(ESP_OK == ret);
    ret = i2c_master_write_byte(cmd, SSD1306_WRITE_CMD, true);
    assert(ESP_OK == ret);
    ret = i2c_master_write(cmd, data, data_len, true);
    assert(ESP_OK == ret);
    ret = i2c_master_stop(cmd);
    assert(ESP_OK == ret);
    ret = i2c_master_cmd_begin(device->bus, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
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



void ssd1306_init(i2c_port_t bus, uint16_t dev_addr)
{
    ssd1306_dev_t *dev = (ssd1306_dev_t *) calloc(1, sizeof(ssd1306_dev_t));
    dev->bus = bus;
    dev->dev_addr = dev_addr << 1;
    ssd1306__init((ssd1306_handle_t) dev);

}

#endif