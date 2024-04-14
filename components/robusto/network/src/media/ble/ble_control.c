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

static char *ble_init_log_prefix;
queue_context_t * ble_media_queue;

void robusto_ble_shutdown() {
    ROB_LOGI(ble_init_log_prefix, "Shutting down BLE:");
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

}
void ble_do_on_work_cb(media_queue_item_t *work_item) {
    
    ROB_LOGD(ble_init_log_prefix, ">> In ESP-NOW work callback.");
    send_work_item(work_item, &(work_item->peer->ble_info), robusto_mt_espnow, 
        &ble_send_message, &ble_do_on_poll_cb, ble_get_queue_context());
}


/**
 * @brief Initialize the  BLE server
 *
 * @param ble_init_log_prefix The prefix for logging and naming
 * @param pvTaskFunction A function containing the task to run
 */
void robusto_ble_init(char *_log_prefix)
{
    ble_init_log_prefix = _log_prefix;
    ROB_LOGI(ble_init_log_prefix, "Initialising BLE..");

    // Note: NVS is not initiated here butin the main initiation

    /* Initialize the host stack */
    nimble_port_init();

    // TODO: Check out if ESP_ERROR_CHECK could't be used.
    // Server as well
    int ret = new_gatt_svr_init();
    assert(ret == 0);

    /* Initialize service */
    ble_init_service(ble_init_log_prefix);
    
    // Print out the address
    rob_mac_address ble_mac_addr;
    esp_base_mac_addr_get(ble_mac_addr);
    ROB_LOGI(ble_init_log_prefix, "BLE base MAC address:");
    rob_log_bit_mesh(ROB_LOG_INFO, ble_init_log_prefix, ble_mac_addr, ROBUSTO_MAC_ADDR_LEN);

    /* Register custom service */
    ret = gatt_svr_register();
    assert(ret == 0);

    
    init_ble_global(_log_prefix);
    /* TODO: Add setting for stack size (it might need to become bigger) */

    /* Initialize the NimBLE host configuration. */

    /* Configure the host callbacks */

    ble_hs_cfg.reset_cb = &ble_on_reset;
    ble_hs_cfg.sync_cb = &ble_spp_server_on_sync;
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

    /* XXX Need to have template for store */
    ble_store_config_init();
  
    /* Start the thread for the host stack, pass the client task which nimble_port_run */
    nimble_port_freertos_init(&ble_host_task);

    add_host_supported_media_type(robusto_mt_ble);
    ROB_LOGI(ble_init_log_prefix, "Initialize BLE queue.");
    ble_media_queue = create_media_queue(ble_init_log_prefix, "BLE worker", &ble_do_on_work_cb, &ble_do_on_poll_cb);
    ROB_LOGI(ble_init_log_prefix, "BLE initialized.");
}

#endif