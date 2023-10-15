#include "conductor_server.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER

#include <robusto_init.h>
#include <robusto_sleep.h>
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_conductor.h>


static char *conductor_server_log_prefix;

void run_conductor_server() {
    ROB_LOGW(conductor_server_log_prefix, "Doing nothing, giving control to conductor");
    robusto_conductor_server_take_control();
}

void init_conductor_server(char *_log_prefix)
{
    conductor_server_log_prefix = _log_prefix;
    robusto_conductor_server_init(_log_prefix, NULL);
}

#endif