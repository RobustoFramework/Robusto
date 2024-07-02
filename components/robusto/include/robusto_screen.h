#include <robconfig.h>
#include <robusto_retval.h>
#ifdef CONFIG_ROBUSTO_UI
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif
lv_disp_t * robusto_screen_lvgl_get_active_display();
bool robusto_screen_lvgl_port_lock(int i);
void robusto_screen_lvgl_port_unlock();

rob_ret_val_t robusto_screen_init(char *_log_prefix);


#ifdef __cplusplus
} /* extern "C" */
#endif
#endif
