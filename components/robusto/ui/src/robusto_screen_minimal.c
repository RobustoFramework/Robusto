#include "robusto_screen_minimal.h"
#ifdef CONFIG_ROBUSTO_UI_MINIMAL
#include <stdio.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#include "ssd1306.h"
#include <stdint.h>
#include <string.h>
SSD1306_t * dev;

static char *minimal_log_prefix;

void robusto_screen_minimal_write(char * txt, uint8_t col, uint8_t row) {
    if (dev) {
        
        ssd1306_display_text(dev, row, txt, strlen(txt), false);
    }
}

void robusto_screen_minimal_clear() {
    if (dev) {
        ssd1306_clear_screen(dev, false);       
    }
    
}

void robusto_screen_minimal_init(char *_log_prefix)
{
    minimal_log_prefix = _log_prefix;
    ROB_LOGI(minimal_log_prefix, "Start Minimal UI.");
    dev = malloc(sizeof(SSD1306_t));
    
#if CONFIG_I2C_INTERFACE
	ROB_LOGI(minimal_log_prefix, "INTERFACE is i2c");
	ROB_LOGI(minimal_log_prefix, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ROB_LOGI(minimal_log_prefix, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ROB_LOGI(minimal_log_prefix, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
#endif // CONFIG_I2C_INTERFACE


#if CONFIG_SSD1306_128x64
	ROB_LOGI(tag, "Panel is 128x64");
	ssd1306_init(&dev, 128, 64);
#endif // CONFIG_SSD1306_128x64
#if CONFIG_SSD1306_128x32
	ROB_LOGI(minimal_log_prefix, "Panel is 128x32");
	ssd1306_init(dev, 128, 32);
#endif // CONFIG_SSD1306_128x32

	ssd1306_clear_screen(dev, false);
	ssd1306_contrast(dev, 0xff);
	//ssd1306_display_text_x3(dev, 0, "Hello", 5, false);

#if 0

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)CONFIG_ROBUSTO_UI_MINIMAL_GPIO_SDA;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = (gpio_num_t)CONFIG_ROBUSTO_UI_MINIMAL_GPIO_SCL;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = CONFIG_ROBUSTO_UI_MINIMAL_I2C_FREQ_HZ;
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
    #if CONFIG_ROBUSTO_UI_MINIMAL_GPIO_RST > -1
    robusto_gpio_set_direction(CONFIG_ROBUSTO_UI_MINIMAL_GPIO_RST, true);
    robusto_gpio_set_level(CONFIG_ROBUSTO_UI_MINIMAL_GPIO_RST, 0);
    r_delay(20);
    robusto_gpio_set_level(CONFIG_ROBUSTO_UI_MINIMAL_GPIO_RST, 1);
    #endif
    
    i2c_param_config(CONFIG_ROBUSTO_UI_MINIMAL_I2C_PORT, &conf);

    i2c_driver_install(CONFIG_ROBUSTO_UI_MINIMAL_I2C_PORT, conf.mode, 0, 0, 0);

   

    #ifdef CONFIG_ROBUSTO_UI_MINIMAL_INTROd
    char data_str[16];
    // sprintf(data_str, CONFIG_ROBUSTO_UI_BANNER);
    sprintf(data_str, "   AAAAAAAAA");
    ssd1306_draw_string(ssd1306_dev, 20, 8, &data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);

    /*
    r_delay(80);
    sprintf(data_str, "     --");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "    -=U=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "   -=RUO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, " -=ROUTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=ROBUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);

    r_delay(600);
    sprintf(data_str, "*=ROBUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-*ROBUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=*OBUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(40);
    sprintf(data_str, "-=R*BUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(20);
    sprintf(data_str, "-=RO*USTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(20);
    sprintf(data_str, "-=ROB*STO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(20);
    sprintf(data_str, "-=ROBU*TO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(40);
    sprintf(data_str, "-=ROBUS*O=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=ROBUST*=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=ROBUSTO*-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=ROBUSTO=*");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    r_delay(80);
    sprintf(data_str, "-=ROBUSTO=-");
    ssd1306_draw_string(ssd1306_dev, 20, 8, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    */
    /*
    r_delay(500);
    sprintf(data_str, "   v %s", ROBUSTO_VERSION); 
    ssd1306_draw_string(ssd1306_dev, 20, 32, (const uint8_t *)data_str, 8, 1);
    ssd1306_refresh_gram(ssd1306_dev);
    */
    #endif
#endif
    ROB_LOGI(minimal_log_prefix, "Minimal UI started.");
};

#endif