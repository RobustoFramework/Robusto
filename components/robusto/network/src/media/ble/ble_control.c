/**
 * @file ble_init.c
 * @brief This is the BLE initialization routines
 *
 */

#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE

#include "ble_control.h"
#include <robusto_message.h>
#include <robusto_media_def.h>

#include <host/ble_hs.h>
#include "ble_spp.h" // This needs to be after ble_hs.h

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_mac.h>
#include <esp_nimble_hci.h>
#include <services/gap/ble_svc_gap.h>
#include "../queue/media_queue.h"

#include "ble_client.h"
#include "ble_service.h"
#include "ble_server.h"
#include <robusto_media.h>


#include "ble_global.h"
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <robusto_peer_def.h>
#include <robusto_logging.h>
#include "esp_heap_trace.h"
static char *ble_init_log_prefix;
static queue_context_t *ble_media_queue;

void ble_set_queue_blocked(bool blocked) {
    set_queue_blocked(ble_media_queue,blocked);
}
void robusto_ble_shutdown() {
    ROB_LOGI(ble_init_log_prefix, "Shutting down BLE:");
    ble_set_queue_blocked(true);
    ROB_LOGI(ble_init_log_prefix, " - freertos deinit");
    nimble_port_freertos_deinit();
    ROB_LOGI(ble_init_log_prefix, " - port deinit");
    nimble_port_deinit();
    ROB_LOGI(ble_init_log_prefix, "BLE shut down.");
}



queue_context_t *ble_get_queue_context() {
    return ble_media_queue;
}

void ble_do_on_poll_cb(queue_context_t *q_context) {
    ROB_LOGD(ble_init_log_prefix, ">> In BLE poll.");
}
void ble_do_on_work_cb(media_queue_item_t *work_item) {
    
    ROB_LOGD(ble_init_log_prefix, ">> In BLE work callback.");
    send_work_item(work_item, &(work_item->peer->ble_info), robusto_mt_ble, 
        &ble_send_message, &ble_do_on_poll_cb, ble_get_queue_context());
}


void ble_sync_callback(void) {
   ble_spp_server_on_sync();
    if (!robusto_waitfor_byte(ble_server_get_state_ptr(), robusto_ble_advertising, 5000)) {
        ROB_LOGW(ble_init_log_prefix, "BLE never started to advertise, will still start the client, state = %hu.", *ble_server_get_state_ptr());  
    }
   ROB_LOGW(ble_init_log_prefix, "BLE is now advertising, lets start discovering others.");
   ble_spp_client_on_sync();
}

/**
 * @brief Initialize the BLE server
 *
 * @param ble_init_log_prefix The prefix for logging and naming
 * @param pvTaskFunction A function containing the task to run
 */
void robusto_ble_init(char *_log_prefix)
{

    ble_init_log_prefix = _log_prefix;
        
    ROB_LOGI(ble_init_log_prefix, "Initialising BLE..");

    ROB_LOGI(ble_init_log_prefix, "Initialize BLE queue (stopped).");
    ble_media_queue = create_media_queue(ble_init_log_prefix, "BLE worker", &ble_do_on_work_cb, &ble_do_on_poll_cb);
    if (!ble_media_queue) {
        ROB_LOGE(ble_init_log_prefix, "Failed to create the BLE media queue. Shutting down BLE, Robusto will report that it is an invalid media type.");
        return;
    }
    // Note: NVS is not initiated here butin the main initiation
  //  ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
    /* Initialize the host stack */
    nimble_port_init();

    
    // Print out the address
    rob_mac_address ble_mac_addr;
    esp_base_mac_addr_get(ble_mac_addr);
    ROB_LOGI(ble_init_log_prefix, "BLE base MAC address:");
    rob_log_bit_mesh(ROB_LOG_INFO, ble_init_log_prefix, ble_mac_addr, ROBUSTO_MAC_ADDR_LEN);
    // Server as well
    init_ble_gatt_svr(ble_init_log_prefix, (rob_mac_address*)ble_mac_addr);
    
    /* Initialize service */
    ble_init_service(ble_init_log_prefix);


      
    /* Register custom service */
    int ret = gatt_svr_register();
    assert(ret == 0);
    
    ble_global_init(_log_prefix);
    ble_spp_client_init(_log_prefix);
    /* TODO: Add setting for stack size (it might need to become bigger) */

    /* Initialize the NimBLE host configuration. */

    /* Configure the host callbacks */

    ble_hs_cfg.reset_cb = &ble_on_reset;
    ble_hs_cfg.sync_cb = &ble_sync_callback;
    
    /*
        if (is_controller)
    {
        ble_hs_cfg.sync_cb = ble_spp_client_on_sync;
    }
    else
    {
        ble_hs_cfg.sync_cb = ble_spp_server_on_sync;
    }
    */
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    // Not secure connections
    ble_hs_cfg.sm_sc = 0;
    /* Initialize data structures to track connected peers.
    There is a local pool in spp.h */
    ROB_LOGI(ble_init_log_prefix, "Init peer with %i max connections.", MYNEWT_VAL(BLE_MAX_CONNECTIONS)); 
    ret = ble_peer_init(ble_init_log_prefix, MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(ret == 0);
 
    /* Generate and set the GAP device name. */
    char gapname[50] = "\0";
    strcpy(gapname, ble_init_log_prefix);
    ret = ble_svc_gap_device_name_set(strncat(strlwr(gapname), "-cli", 100));
    assert(ret == 0);


    ble_server_init(_log_prefix);

    /* Need to have template for store */
    ble_store_config_init();

    /* Start the thread for the host stack, pass the client task which nimble_port_run */
    nimble_port_freertos_init(&ble_host_task);

    if (!robusto_waitfor_byte(ble_server_get_state_ptr(), robusto_ble_advertising, 15000)) {
        ROB_LOGE(ble_init_log_prefix, "BLE never started to advertise, state = %hu.", *ble_server_get_state_ptr());  
        robusto_ble_shutdown();
        return;  
    }
    // TODO: Present PubSub using dynamic BLE servives. "Enable dynamic services"
    add_host_supported_media_type(robusto_mt_ble);

    ROB_LOGI(ble_init_log_prefix, "Advertising; unblocking the BLE queue. Task: %s", ble_media_queue->worker_task_name);
    ble_set_queue_blocked(false);
    ROB_LOGI(ble_init_log_prefix, "BLE running.");

}

#endif