#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#ifdef USE_ESPIDF
#include <host/ble_hs.h>
#include "ble_spp.h"

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <esp32/rom/crc.h>
#include <robusto_logging.h>

#include "ble_service.h"
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_message.h>

#include "ble_global.h"


int send_status;

/* This semaphore is use for blocking, so different threads doesn't accidentaly communicate at the same time */
SemaphoreHandle_t xBLE_Comm_Semaphore;
// TODO: The semaphore might not be needed when this is behind the queue, the receive is probably not conflicting?
char *ble_global_log_prefix;

void ble_to_base_mac_address(uint8_t *mac_address, uint8_t *dest_address)
{
    memcpy(dest_address, mac_address + 5, 1);
    memcpy(dest_address + 1, mac_address + 4, 1);
    memcpy(dest_address + 2, mac_address + 3, 1);
    memcpy(dest_address + 3, mac_address + 2, 1);
    memcpy(dest_address + 4, mac_address + 1, 1);
    uint8_t tmp = *mac_address - 2;
    memcpy(dest_address + 5, &tmp, 1);
}

void base_mac_to_ble_address(uint8_t *mac_address, uint8_t *dest_address)
{
    uint8_t tmp = *(mac_address + 5) - 2;
    memcpy(dest_address, &tmp, 1);
    memcpy(dest_address + 1, mac_address + 4, 1);
    memcpy(dest_address + 2, mac_address + 3, 1);
    memcpy(dest_address + 3, mac_address + 2, 1);
    memcpy(dest_address + 4, mac_address + 1, 1);
}

/**
 * @brief The general client host task
 * @details This is the actual task the host runs in.
 * TODO: Does this have to be sticked to core 0? Set in the config?
 */
void ble_host_task(void *param)
{
    ROB_LOGI(ble_global_log_prefix, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

void ble_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

/**
 * Called when service discovery of the specified peer has completed.
 */

void ble_on_disc_complete(const struct ble_peer *peer, int status, void *arg)
{

    if (status != 0)
    {
        /* Service discovery failed.  Terminate the connection. */
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d "
                           "conn_handle=%d\n",
                    status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    ROB_LOGI(ble_global_log_prefix, "ble_on_disc_complete");
    rob_log_bit_mesh(ROB_LOG_INFO, ble_global_log_prefix, &(peer->desc.our_ota_addr.val), ROBUSTO_MAC_ADDR_LEN);

    // Find out a base mac address from the reversed one
    rob_mac_address *reversed_address = robusto_malloc(ROBUSTO_MAC_ADDR_LEN);
    ble_to_base_mac_address(&(peer->desc.peer_ota_addr.val), reversed_address);
    ROB_LOGI(ble_global_log_prefix, "reversed address, based on peer_ota_addr");
    rob_log_bit_mesh(ROB_LOG_INFO, ble_global_log_prefix, reversed_address, ROBUSTO_MAC_ADDR_LEN);

    ROB_LOGI(ble_global_log_prefix, "Checking BLE peer for a robusto compatible service. UUID:");
    ble_uuid128_t *uuid = create_mac_on_sec_test_uuid(reversed_address);
    rob_log_bit_mesh(ROB_LOG_INFO, ble_global_log_prefix, uuid->value, 16);
    if (ble_peer_svc_find_uuid(peer, uuid))
    {
        /* Remember peer. */
        ROB_LOGI(ble_global_log_prefix, "Peer had the SPP-service, adding as Robusto peer");

        // Add a robusto-peer that contains the information

        robusto_peer_t *_robusto_peer = robusto_add_init_new_peer(NULL, reversed_address, robusto_mt_ble);
        _robusto_peer->ble_conn_handle = peer->conn_handle;
    }
    else
    {
        ROB_LOGI(ble_global_log_prefix, "Peer didn't have the SPP-service, will not add as Robusto peer:");
        rob_log_bit_mesh(ROB_LOG_INFO, ble_global_log_prefix, reversed_address, ROBUSTO_MAC_ADDR_LEN);
    }
    robusto_free(reversed_address);
    robusto_free(uuid);

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    MODLOG_DFLT(INFO, "Service discovery complete; status=%d "
                      "conn_handle=%d\n",
                status, peer->conn_handle);
}

/**
 * @brief Sends an MTU negotiation request to the peer.
 * Makes it accept the proposed MTU (it will not start *sending* using that)
 * Goes by the MTU settings in menuconfig.
 *
 * @param conn_handle
 * @return int
 */
int ble_negotiate_mtu(uint16_t conn_handle)
{

    MODLOG_DFLT(INFO, "Negotiate MTU.\n");

    int rc = ble_gattc_exchange_mtu(conn_handle, NULL, NULL);
    if (rc != 0)
    {

        MODLOG_DFLT(ERROR, "Error from exchange_mtu; rc=%d\n", rc);

        return rc;
    }
    MODLOG_DFLT(INFO, "MTU exchange request sent.\n");

    vTaskDelay(10);
    return 0;
}

/**
 * @brief Optional callback for transmitting gatt messages, use in ble_gattc_write_*-calls.
 *
 * @param conn_handle
 * @param error
 * @param attr
 * @param arg
 * @return int
 */
int ble_gatt_cb(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr,
                void *arg)
{

    ROB_LOGI(ble_global_log_prefix, "ble_gattc_write_ callback: error.status: %i error.att_handle: %i attr.handle: %i attr.offset: %i",
             error->status, error->att_handle, attr->handle, attr->offset);
    return 0;
}

// TODO: It seems slightly strange that this is here
void report_ble_connection_error(int conn_handle, int code)
{
    struct ble_peer *b_peer = ble_peer_find(conn_handle);

    if (b_peer != NULL)
    {
        struct robusto_peer *s_peer = robusto_peers_find_peer_by_handle(b_peer->robusto_handle);
        if (s_peer != NULL)
        {
            ROB_LOGE(ble_global_log_prefix, "Peer %s encountered a BLE error. Code: %i", s_peer->name, code);
        }
        else
        {
            ROB_LOGE(ble_global_log_prefix, "Unresolved BLE peer (no or invalid SDP peer handle), conn handle %i", conn_handle);
            ROB_LOGE(ble_global_log_prefix, "encountered a BLE error. Code: %i.", code);
        }
        b_peer->failure_count++;
    }
    ROB_LOGE(ble_global_log_prefix, "Unregistered peer (!) at conn handle %i encountered a BLE error. Code: %i.", conn_handle, code);
}

void ble_send_cb(uint16_t conn_handle,
                 const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attr,
                 void *arg)
{
    send_status = error->status;
}

rob_ret_val_t ble_send_message_raw(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
    if (pdTRUE == xSemaphoreTake(xBLE_Comm_Semaphore, portMAX_DELAY))
    {   
        int ret = BLE_HS_EAGAIN; // Setting to 1 to handle BLE_HS_EAGAIN == Temporary errr
        int retries = -1;
        if (receipt)
        {
            send_status = -1;
            while ((ret == BLE_HS_EAGAIN) && (retries < 3)) {
                retries++;
                if (retries > 0) {
                    ROB_LOGW(ble_global_log_prefix, "ble_send_message: Retrying dues to BLE being busy");
                    r_delay(500);
                }
                if (data_length == 8) {
                    rob_log_bit_mesh(ROB_LOG_INFO, ble_global_log_prefix,data, data_length);
                }
                
                ret = ble_gattc_write_flat(peer->ble_conn_handle, get_ble_spp_svc_gatt_read_val_handle(), data, data_length, ble_send_cb, NULL);
            }
                 
            if ((ret == ESP_OK) && (!robusto_waitfor_int_change(&send_status, 10000))) {
                ROB_LOGW(ble_global_log_prefix, "ble_send_message: send failed waiting for result. send_status = %i (setting return code to %i)", send_status, -send_status);
                add_to_history(&peer->ble_info, true, send_status);
                ret = -send_status;
            } 
        }
        else
        {
            retries++;
            ret = ble_gattc_write_no_rsp_flat(peer->ble_conn_handle, get_ble_spp_svc_gatt_read_val_handle(), data, data_length);
            robusto_yield();
        }

        if (ret == ESP_OK)
        {
            ROB_LOGI(ble_global_log_prefix, "ble_send_message: Success sending %lu bytes of data! CRC32: %lu", data_length, crc32_be(0, data , data_length ));
            add_to_history(&peer->ble_info, true, ROB_OK);
            ret = ROB_OK;
        }
        else
        {
            ROB_LOGE(ble_global_log_prefix, "Error: ble_send_message  - Failure when sending data! Connection handle: %i Return code: %i Retries: %i Receipt: %s", peer->ble_conn_handle, ret, retries, receipt ? "Yes": "No");
            add_to_history(&peer->ble_info, true, ret);
            ret = -ret;
        }
        xSemaphoreGive(xBLE_Comm_Semaphore);
        return ret;
    }
    else
    {
        ROB_LOGE(ble_global_log_prefix, "Error: ble_send_message  - Couldn't get semaphore!");
        return ROB_ERR_MUTEX;
    }

}

/**
 * @brief Sends a message through BLE.
 */
rob_ret_val_t ble_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
#if CONFIG_ROB_NETWORK_TEST_BLE_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_BLE_KILL_SWITCH) == true)
    {
        ROB_LOGE("BLE", "BLE KILL SWITCH ON - Failing sending");
        r_delay(100);
        return ROB_FAIL;
    }
#endif
    if (data_length > (CONFIG_NIMBLE_ATT_PREFERRED_MTU + ROBUSTO_PREFIX_BYTES - 10))
    {
        ROB_LOGI(ble_global_log_prefix, "Data length %lu is more than cutoff at %i bytes, sending fragmented", data_length, CONFIG_NIMBLE_ATT_PREFERRED_MTU - 10);
        return send_message_fragmented(peer, robusto_mt_ble, data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES,
             CONFIG_NIMBLE_ATT_PREFERRED_MTU - 20, ble_send_message_raw);
    }
    return ble_send_message_raw(peer, data + ROBUSTO_PREFIX_BYTES, data_length - ROBUSTO_PREFIX_BYTES, receipt);
}

void ble_global_init(char *_log_prefix)
{
    ble_global_log_prefix = _log_prefix;
    /* Create mutexes for blocking during BLE operations */
    xBLE_Comm_Semaphore = xSemaphoreCreateMutex();
    init_ble_spp(_log_prefix);
}

#endif
#endif