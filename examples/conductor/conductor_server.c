#include "conductor_server.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_SERVER

#include <robusto_init.h>
#include <robusto_sleep.h>
#include <robusto_logging.h>
#include <robusto_message.h>

static robusto_peer_t *r_peer;

static char *client_log_prefix;

void conductor_client_call_server()
{

    if (r_peer->state == PEER_UNKNOWN)
    {
        ROB_LOGE(client_log_prefix, "Peer still unknown, not calling it, presentation may have failed");
    }
    else
    {

        queue_state *q_state = NULL;
        q_state = robusto_malloc(sizeof(queue_state));

        rob_ret_val_t ret_val_flag;
        char *message = "Hi there!";

        rob_ret_val_t queue_ret = send_message_strings(r_peer, 0, 0, (uint8_t *)message, 10, q_state);
        if (queue_ret != ROB_OK)
        {
            ROB_LOGE(client_log_prefix, "Error queueing message: %i", queue_ret);
            ret_val_flag = queue_ret;
        }
        else if (!robusto_waitfor_queue_state(q_state, 6000, &ret_val_flag))
        {
            // robusto_media_t *info = get_media_info(r_peer, r_peer->last_selected_media_type);
            ROB_LOGE(client_log_prefix, "Failed sending hello, queue state %hhu , reason code: %hi", *(uint8_t *)q_state[0], ret_val_flag);
        }
        else
        {
            ROB_LOGW(client_log_prefix, "Successfully queued hello message (not waiting for receipt)!");
        }
        q_state = robusto_free_queue_state(q_state);
    }
    ROB_LOGW(client_log_prefix, "GOING TO SLEEP FOR 5 seconds");
    robusto_goto_sleep(5000);
}

void init_conductor_client(char *_log_prefix)
{
    client_log_prefix = _log_prefix;

}

#endif