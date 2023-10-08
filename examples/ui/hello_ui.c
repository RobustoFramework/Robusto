#include "hello_ui.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_UI

#include <robusto_screen.h>
#include <robusto_screen_minimal.h>
#include <robusto_time.h>

char * hello_ui_log_prefix;

void start_hello_ui(char * _log_prefix)
{
    hello_ui_log_prefix = _log_prefix;
    robusto_screen_init(hello_ui_log_prefix);
    r_delay(1000);
    robusto_screen_minimal_write("Test One", 0, 0);
    r_delay(1000);
    robusto_screen_minimal_write("and Two", 9, 0);
    r_delay(1000);
    robusto_screen_minimal_write("..and HELLOO!!", 1, 3);
}



#endif