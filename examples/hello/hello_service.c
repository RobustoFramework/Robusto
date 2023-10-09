#include "hello_service.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_SERVER 

#include <robusto_init.h>
#include <robusto_logging.h>
#include <robusto_incoming.h> 
#include <robusto_network_service.h>
#include <robusto_message.h>
#include <robusto_queue.h>
#include <string.h>

static robusto_peer_t *r_peer;
const uint16_t serviceid = 1959;

void register_hello_service()__attribute__((constructor));

void on_incoming(robusto_message_t *message);
void shutdown_hello_network_service(void);


char * hello_log_prefix;

char _service_name[13] = "Hello";

network_service_t hello_service = {
    service_id : serviceid,
    service_name : &_service_name,
    incoming_callback : &on_incoming,
    shutdown_callback: &shutdown_hello_network_service
};

void on_incoming(robusto_message_t *message) {
    
    if (message->string_count == 1) {
        if (strcmp(message->strings[0], "Hi there!") == 0) {
            ROB_LOGW(hello_log_prefix, "Got a message from the %s client through %s!", message->peer->name, media_type_to_str(message->media_type));
            char * response = "Well hello!!\x00";
            send_message_strings(message->peer, 0,0, (uint8_t *)response, 13, NULL);

        } else {
            ROB_LOGE(hello_log_prefix, "A message, but not hello: %s", message->strings[0]);
        }
    } else {
        ROB_LOGE(hello_log_prefix, "Got a message that didn't have one string!");
    }
    
}

void shutdown_hello_service(void) {

    ROB_LOGE(hello_log_prefix, "Hello service shutdown.");
}


void shutdown_hello_network_service(void) {

    ROB_LOGE(hello_log_prefix, "Hello network service shutdown.");
}

void start_hello_service(char * _log_prefix)
{
    if (robusto_register_network_service(&hello_service) != ROB_OK) {
        ROB_LOGE(hello_log_prefix, "Failed adding service");
    }
}

void init_hello_service(char * _log_prefix)
{
    hello_log_prefix = _log_prefix;

}

void incoming_client(incoming_queue_item_t *incoming_item) {
    if (incoming_item->message->string_count == 1) {
        if (strcmp(incoming_item->message->strings[0], "Well hello!!") == 0) {
            ROB_LOGW(hello_log_prefix, "Got a message from the %s peer = %s using media type %s!", 
                incoming_item->message->peer->name, incoming_item->message->strings[0], media_type_to_str(incoming_item->message->media_type)); 
        }
    } else {
        ROB_LOGW(hello_log_prefix, "Got a message but didn't seem to be a reply from an example server");
    }  
}
void register_hello_service() {
    register_service(init_hello_service, start_hello_service, shutdown_hello_service, 4,  "Hello service");    
}

#ifdef CONFIG_ROBUSTO_EXAMPLE_HELLO_CLIENT
void init_hello_client(char * _log_prefix)
{
    hello_log_prefix = _log_prefix;
    robusto_register_handler(incoming_client);

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
        ROB_LOGE(hello_log_prefix, "Failed adding the peer.");
        return;
    }


}
void hello_client_call_server() {

    if (r_peer->state == PEER_UNKNOWN) { 
        ROB_LOGE(hello_log_prefix, "Peer still unknown, not calling it, presentation may have failed");
        return;
    }
    queue_state *q_state = NULL;
    q_state = robusto_malloc(sizeof(queue_state));
    
    rob_ret_val_t ret_val_flag;
    char * message = "Hi there!";
    
    rob_ret_val_t queue_ret = send_message_strings(r_peer, serviceid, 0, (uint8_t *)message, 10, q_state);
    if (queue_ret != ROB_OK) {
        ROB_LOGE(hello_log_prefix, "Error queueing message: %i", queue_ret);
        ret_val_flag = queue_ret;
    } else
    if (!robusto_waitfor_queue_state(q_state, 6000, &ret_val_flag)) {
        //robusto_media_t *info = get_media_info(r_peer, r_peer->last_selected_media_type);
        ROB_LOGE(hello_log_prefix, "Failed sending hello, queue state %hhu , reason code: %hi", *(uint8_t*)q_state[0], ret_val_flag);
    } else {
        ROB_LOGW(hello_log_prefix, "Successfully queued hello message (not waiting for receipt)!");
    }
    q_state = robusto_free_queue_state(q_state);
   

}

#endif
#endif