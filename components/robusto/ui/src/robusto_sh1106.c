#include "robusto_sh1106.h"
#ifdef CONFIG_ROBUSTO_UI
#include <sys/cdefs.h>
#include <esp_lcd_panel_io.h>
#include <robusto_logging.h>

#define LCD_SH1106_I2C_CMD  0X00

esp_err_t panel_sh1106_init(esp_lcd_panel_t *panel)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    int cmd = 0;
    while (vendor_specific_init[cmd] != 0xff) {
        esp_lcd_panel_io_tx_param(io, LCD_SH1106_I2C_CMD, (uint8_t[]) {
            vendor_specific_init[cmd]
        }, 1); 
        cmd++;
    }

    return ESP_OK;
}

#endif