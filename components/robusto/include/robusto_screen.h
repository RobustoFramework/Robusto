#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UI
#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif
lv_disp_t * robusto_screen_lvgl_get_active_display();

void robusto_screen_init(char * _log_prefix);


#ifdef __cplusplus
} /* extern "C" */
#endif
#endif
