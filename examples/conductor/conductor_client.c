#include "conductor_client.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_CLIENT

#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_conductor.h>


static char *conductor_server_log_prefix;

void conductor_client_call_server()
{

    robusto_peer_t * conductor_peer = robusto_conductor_client_get_conductor();
    if (conductor_peer) {   
        if (r_millis() < (CONFIG_ROBUSTO_CONDUCTOR_RETRY_WAIT_S * 1000))
        {

            queue_state *q_state = NULL;
            q_state = robusto_malloc(sizeof(queue_state));

            rob_ret_val_t ret_val_flag;
            char *message = "Hi there!";

            rob_ret_val_t queue_ret = send_message_strings(conductor_peer, 0, 0, (uint8_t *)message, 10, q_state);
            if (queue_ret != ROB_OK)
            {
                ROB_LOGE(conductor_server_log_prefix, "Example: Error queueing message: %i", queue_ret);
                ret_val_flag = queue_ret;
            }
            else if (!robusto_waitfor_queue_state(q_state, 6000, &ret_val_flag))
            {
                // robusto_media_t *info = get_media_info(conductor_peer, conductor_peer->last_selected_media_type);
                ROB_LOGE(conductor_server_log_prefix, "Example: Failed sending hello, queue state %hhu , reason code: %hi", *(uint8_t *)q_state[0], ret_val_flag);
            }
            else
            {
                ROB_LOGW(conductor_server_log_prefix, "Example: Successfully queued hello message (not waiting for receipt)!");
            }
            q_state = robusto_free_queue_state(q_state);
        }
    }
    ROB_LOGW(conductor_server_log_prefix, "Example: Going to sleep until next time");

    robusto_conductor_client_give_control();
}

void init_conductor_client(char *_log_prefix)
{
    conductor_server_log_prefix = _log_prefix;

    char *dest = "The server";

}

#endif