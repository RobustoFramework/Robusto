#include "hello_ui.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI

#include <robusto_screen.h>
#include <robusto_time.h>

#include <lvgl.h>
#ifdef USE_ESPIDF
#include "esp_lvgl_port.h"
#endif

static char *hello_ui_log_prefix;

void start_hello_ui(char *_log_prefix)
{
    hello_ui_log_prefix = _log_prefix;
    robusto_screen_init(hello_ui_log_prefix);

    lv_disp_t *screen = robusto_screen_lvgl_get_active_display();
    if (lvgl_port_lock(0))
    {

        lv_obj_t *scr = lv_disp_get_scr_act(screen);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
        lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
        /* Size of the screen (if you use rotation 90 or 270, please set disp->driver->ver_res) */
        lv_obj_set_width(label, screen->driver->hor_res);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

        lvgl_port_unlock();
    }
    /*
    r_delay(1000);
    robusto_screen_minimal_write("Test One", 0, 0);
    r_delay(1000);
    robusto_screen_minimal_write("and Two", 9, 0);
    r_delay(1000);
    robusto_screen_minimal_write("..and HELLOO!!", 1, 3);
    */
}

#endif