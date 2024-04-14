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

#include "ble_global.h"

/* This semaphore is use for blocking, so different threads doesn't accidentaly communicate at the same time */
SemaphoreHandle_t xBLE_Comm_Semaphore;
// TODO: The semaphore might not be needed when this is behind the queue, the receive is probably not conflicting?
char *ble_global_log_prefix;

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

/**
 * @brief Sends a message through BLE.
 */
rob_ret_val_t ble_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{
    // TODO: Add 
    if (pdTRUE == xSemaphoreTake(xBLE_Comm_Semaphore, portMAX_DELAY))
    {
        int ret;
        if (receipt) {
            ret = ble_gattc_write_flat(peer->ble_conn_handle, get_ble_spp_svc_gatt_read_val_handle(), data, data_length, NULL, NULL);
        } else {
            ret = ble_gattc_write_no_rsp_flat(peer->ble_conn_handle, get_ble_spp_svc_gatt_read_val_handle(), data, data_length);
        }
        
        if (ret == 0)
        {
            ROB_LOGI(ble_global_log_prefix, "ble_send_message: Success sending %lu bytes of data! CRC32: %lu", data_length, crc32_be(0, data, data_length));
        }
        else
        {
            ROB_LOGE(ble_global_log_prefix, "Error: ble_send_message  - Failure when writing data! Peer: %i Code: %i", peer->ble_conn_handle, ret);
            xSemaphoreGive(xBLE_Comm_Semaphore);
            return -ret;
        }
        xSemaphoreGive(xBLE_Comm_Semaphore);
    }
    else
    {
        ROB_LOGE(ble_global_log_prefix, "Error: ble_send_message  - Couldn't get semaphore!");
        return ROB_ERR_MUTEX;
    }
    return 0;
}

void init_ble_global(char *_log_prefix)
{
    ble_global_log_prefix = _log_prefix;
    /* Create mutexes for blocking during BLE operations */
    xBLE_Comm_Semaphore = xSemaphoreCreateMutex();
    init_ble_spp(_log_prefix);
}

#endif
#endif