#include "hello_ui.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI

#include <robusto_screen.h>
#include <robusto_time.h>
#include <robusto_logging.h>
#include <lvgl.h>


static char *hello_ui_log_prefix;

void start_hello_ui(char *_log_prefix)
{
    hello_ui_log_prefix = _log_prefix;
    robusto_screen_init(hello_ui_log_prefix);

    lv_disp_t *display = robusto_screen_lvgl_get_active_display();
    if (robusto_screen_lvgl_port_lock(0))
    {
        lv_obj_t *scr = lv_disp_get_scr_act(display);
        static lv_style_t style_scr;
        lv_style_init(&style_scr);
        lv_style_set_pad_left(&style_scr, 2);   
        
        lv_obj_add_style(scr, &style_scr, LV_STATE_DEFAULT);
    
        lv_obj_t *label = lv_label_create(scr);
        
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
        lv_label_set_text(label, "Robusto framework example. This should look good on the screen...");
        /* Size of the screen (if you use rotation 90 or 270, please set disp->driver->ver_res) */
        #if defined(CONFIG_ROBUSTO_UI_LVGL_LCD_CONTROLLER_SH1106)
        // The sh1106 is often put on 128px screens, but is internally 132 px
        lv_obj_set_width(label, lv_obj_get_width(scr)-4);
        #else
        lv_obj_set_width(label, lv_obj_get_width(scr));
        #endif

        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

        robusto_screen_lvgl_port_unlock();
    }

}

#endif