#include "robusto_screen.h"
#ifdef CONFIG_ROBUSTO_UI

#ifdef CONFIG_ROBUSTO_UI_MINIMAL
#include "robusto_screen_minimal.h"
#endif

char * ui_log_prefix;

void robusto_screen_init(char * _log_prefix) {

    ui_log_prefix = _log_prefix;
    #ifdef CONFIG_ROBUSTO_UI_MINIMAL
    robusto_screen_minimal_init(ui_log_prefix);
    #endif

}

#endif
