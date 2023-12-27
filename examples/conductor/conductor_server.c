#include "conductor_server.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER

#include <robusto_init.h>
#include <robusto_sleep.h>
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_conductor.h>
#include <robusto_incoming.h>

static char *conductor_server_log_prefix;

void on_incoming_conductor_server_example(incoming_queue_item_t *incoming_item)
{
    if (incoming_item->message->string_count > 0) {
        ROB_LOGI(conductor_server_log_prefix, "Example: Peer %s sent us a message: %s", incoming_item->message->peer->name, incoming_item->message->strings[0]);
    } else {
        ROB_LOGI(conductor_server_log_prefix, "Example: Peer %s sent us something.", incoming_item->message->peer->name);
        rob_log_bit_mesh(ROB_LOG_INFO, conductor_server_log_prefix, incoming_item->message->raw_data, incoming_item->message->raw_data_length);
    }
}

void run_conductor_server() {
    ROB_LOGW(conductor_server_log_prefix, "Example: Doing nothing, giving control to conductor");
    robusto_conductor_server_take_control();
}

void init_conductor_server(char *_log_prefix)
{
    conductor_server_log_prefix = _log_prefix;
    robusto_register_handler(&on_incoming_conductor_server_example);
}

#endif