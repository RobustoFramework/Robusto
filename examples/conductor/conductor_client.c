#include "conductor_client.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_CONDUCTOR_CLIENT 

#include <robusto_init.h>
#include <robusto_sleep.h>
#include <robusto_logging.h>
#include <robusto_message.h>

static robusto_peer_t *r_peer;

static char * client_log_prefix;

void conductor_client_call_server() {

    if (r_peer->state == PEER_UNKNOWN) { 
        ROB_LOGE(client_log_prefix, "Peer still unknown, not calling it, presentation may have failed");
        return;
    }
    queue_state *q_state = NULL;
    q_state = robusto_malloc(sizeof(queue_state));
    
    rob_ret_val_t ret_val_flag;
    char * message = "Hi there!";
    
    rob_ret_val_t queue_ret = send_message_strings(r_peer, 0, 0, (uint8_t *)message, 10, q_state);
    if (queue_ret != ROB_OK) {
        ROB_LOGE(client_log_prefix, "Error queueing message: %i", queue_ret);
        ret_val_flag = queue_ret;
    } else
    if (!robusto_waitfor_queue_state(q_state, 6000, &ret_val_flag)) {
        //robusto_media_t *info = get_media_info(r_peer, r_peer->last_selected_media_type);
        ROB_LOGE(client_log_prefix, "Failed sending hello, queue state %hhu , reason code: %hi", *(uint8_t*)q_state[0], ret_val_flag);
    } else {
        ROB_LOGW(client_log_prefix, "Successfully queued hello message (not waiting for receipt)!");
    }
    q_state = robusto_free_queue_state(q_state);
    robusto_goto_sleep(5000);
   

}



void init_conductor_client(char * _log_prefix)
{
    client_log_prefix = _log_prefix;

    char * dest = "The server";
    r_peer = NULL;    
	// Add peer and presentat ourselvers
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_I2C
    r_peer = add_peer_by_i2c_address(dest, CONFIG_ROB_NETWORK_TEST_I2C_CALL_ADDR);
    #endif
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_ESP_NOW
    r_peer = add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_ESP_NOW_CALL_ADDR), robusto_mt_espnow);
    #endif
    #ifdef CONFIG_ROBUSTO_NETWORK_TEST_SELECT_INITIAL_MEDIA_LORA
    r_peer = add_peer_by_mac_address(dest, kconfig_mac_to_6_bytes(CONFIG_ROB_NETWORK_TEST_LORA_CALL_ADDR), robusto_mt_lora);
    #endif
    if (r_peer == NULL) {
        ROB_LOGE(client_log_prefix, "Failed adding the peer.");
        return;
    }


}

#endif