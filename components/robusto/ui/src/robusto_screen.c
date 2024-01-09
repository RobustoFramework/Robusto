#include "robusto_screen.h"
#ifdef CONFIG_ROBUSTO_UI

#include <robusto_logging.h>
#include <robusto_system.h>
#ifdef USE_ESPIDF
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c.h"
#endif

#include "lvgl.h"
#ifdef USE_ESPIDF
#include "esp_lvgl_port.h"
#endif

#ifdef CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SSD1306
#if CONFIG_ROBUSTO_UI_LCD_V_RES == 32
#include "robusto_32px.h"
#endif
#endif

#ifdef CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SH1107
#include "esp_lcd_sh1107.h"
#else
#include "esp_lcd_panel_vendor.h"
#endif
// Bit number used to represent command and parameter
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

lv_disp_t *disp;

static char *ui_log_prefix;

bool robusto_screen_lvgl_port_lock(int i) {
    return lvgl_port_lock(i);
}
void robusto_screen_lvgl_port_unlock() {
    lvgl_port_unlock();
}


void init_i2c()
{

#ifdef USE_ESPIDF
#ifdef CONFIG_ROBUSTO_UI_INIT_I2C
    ROB_LOGI(ui_log_prefix, "Init UI I2C.");
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)CONFIG_ROBUSTO_UI_GPIO_SDA;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = (gpio_num_t)CONFIG_ROBUSTO_UI_GPIO_SCL;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = CONFIG_ROBUSTO_UI_I2C_FREQ_HZ;
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
#if CONFIG_ROBUSTO_UI_GPIO_RST > -1
    robusto_gpio_set_direction(CONFIG_ROBUSTO_UI_GPIO_RST, true);
    robusto_gpio_set_level(CONFIG_ROBUSTO_UI_GPIO_RST, 0);
    r_delay(20);
    robusto_gpio_set_level(CONFIG_ROBUSTO_UI_GPIO_RST, 1);
#endif
    i2c_param_config(CONFIG_ROBUSTO_UI_I2C_PORT, &conf);
    i2c_driver_install(CONFIG_ROBUSTO_UI_I2C_PORT, conf.mode, 0, 0, 0);
#endif
#endif
}

lv_disp_t *robusto_screen_lvgl_get_active_display()
{
    return disp;
}



void robusto_screen_init(char *_log_prefix)
{

    ui_log_prefix = _log_prefix;

#ifdef CONFIG_ROBUSTO_UI_INIT_I2C
    init_i2c();
#endif

#ifdef USE_ESPIDF
    ROB_LOGI(ui_log_prefix, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = CONFIG_ROBUSTO_UI_I2C_HW_ADDR,
        .control_phase_bytes = 1,         // According to SSD1306 datasheet
        .lcd_cmd_bits = LCD_CMD_BITS,     // According to SSD1306 datasheet
        .lcd_param_bits = LCD_PARAM_BITS, // According to SSD1306 datasheet
#if CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SSD1306
        .dc_bit_offset = 6, // According to SSD1306 datasheet
#elif CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SH1107
        .dc_bit_offset = 0, // According to SH1107 datasheet
        .flags =
            {
                .disable_control_phase = 1,
            }
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(CONFIG_ROBUSTO_UI_I2C_PORT, &io_config, &io_handle));

    ROB_LOGI(ui_log_prefix, "Install screen panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = CONFIG_ROBUSTO_UI_GPIO_RST,
    };
    /* TODO: Re-add this when the 32 px height support is released
    #if CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SSD1306
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = CONFIG_ROBUSTO_UI_LCD_V_RES,
        };
        panel_config.vendor_config = &ssd1306_config;
    */

#if CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SSD1306
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
#elif CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#ifdef CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SSD1306
#if CONFIG_ROBUSTO_UI_LCD_V_RES == 32
    ROB_LOGI(ui_log_prefix, "Do 32 pixel workaround");
    ssd1306_init(CONFIG_ROBUSTO_UI_I2C_PORT, CONFIG_ROBUSTO_UI_I2C_HW_ADDR);
#endif
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

#if CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
#endif

    ROB_LOGI(ui_log_prefix, "Initialize LVGL");
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = CONFIG_ROBUSTO_UI_LCD_H_RES * CONFIG_ROBUSTO_UI_LCD_V_RES,
        .double_buffer = true,
        .hres = CONFIG_ROBUSTO_UI_LCD_H_RES,
        .vres = CONFIG_ROBUSTO_UI_LCD_V_RES,
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        }};
    disp = lvgl_port_add_disp(&disp_cfg);

    /* Rotation of the screen */
    lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);

    ROB_LOGI(ui_log_prefix, "LVGL initiated");
}

#endif
