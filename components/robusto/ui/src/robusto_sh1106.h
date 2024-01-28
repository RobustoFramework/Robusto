/**
 * @file robusto_sh1106.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief This is 
 * @version 0.1
 * @date 2024-01-28
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UI
#include <esp_err.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_types.h>


typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    unsigned int bits_per_pixel;
    bool swap_axes;
} sh1106_panel_t;

static const uint8_t vendor_specific_init[] = {
    0xAE,   /* turn off OLED display */
    
    0xd5,   /* set osc division */
    0x80,

    0xa8,   /* multiplex ratio */
    0x3f,   /* duty = 1/64 */

    0xBF,   /* Set page address */
    0xd3,   /* set display offset */
    0x00,  
    
    0x40,   /* set display start line */

    0xad, /* set charge pump */
    0x8b,

    0xA0,   /* Set segment re-map - Non-flipped horizontal */
    0xC7,   /* Set COM output scan direction Non-flipped vertical */

    0xDA,   /* Set com pins */
    0x12,

    0x81,   /* contrast control */
    0x2f,   /* 128 */
    
    0xD9, /* set pre-charge period */
    0X22,

    0xdb,   /* set vcomh deselect */
    0x40,
    0x20,   /* Set Memory addressing mode (0x20/0x21) */
    0x32, /* set VPP */

//    0xa4,   /* output ram to display */

    0xa6,   /* normal / inverted colors */

    0xFF, //END
};

esp_err_t panel_sh1106_init(esp_lcd_panel_t *panel);

#endif