
/***********************
 * This is general server level handling
 ***********************/
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#ifdef USE_ESPIDF

#include <host/util/util.h>
#include <host/ble_hs.h>

#include <esp_nimble_hci.h>
#include <services/gap/ble_svc_gap.h>

#include <robusto_logging.h>

#include "ble_server.h"
#include "ble_global.h"
#include "ble_spp.h"


char * ble_server_log_prefix;
static uint8_t own_addr_type;
static uint8_t ble_srv_state = robusto_ble_stopped;

static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg);

ble_server_state_t *ble_server_get_state_ptr() {
    return &ble_srv_state;
}


/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
ble_spp_server_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    ROB_LOGE(ble_server_log_prefix, "ble_svc_gap_device_name  = %s\n", name);
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]){
        BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)};
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ROB_LOGE(ble_server_log_prefix, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_spp_server_gap_event, NULL);
    if (rc != 0)
    {
        ROB_LOGE(ble_server_log_prefix, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
    ble_srv_state = robusto_ble_advertising;
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * ble_spp_server uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_spp_server.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
ble_spp_server_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ROB_LOGI(ble_server_log_prefix, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0)
        {
            /* Connection successfully established. */
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            ROB_LOGI(ble_server_log_prefix, "\n");

            rc = ble_negotiate_mtu(event->connect.conn_handle);
            if (rc != 0)
            {
                ROB_LOGE(ble_server_log_prefix, "Server:Failed to negotiate MTU; rc=%d\n", rc);
                return 0;
            }
            
            
            rc = ble_peer_add(event->connect.conn_handle, desc);
            if (rc != 0)
            {
                if (rc == BLE_HS_EALREADY) {
                    ROB_LOGI(ble_server_log_prefix, "Peer was already present (conn_handle=%i).", event->connect.conn_handle);

                } else {
                    ROB_LOGE(ble_server_log_prefix, "Failed to add peer; rc=%d\n", rc);
                    
                }

            }
            ROB_LOGI(ble_server_log_prefix, "Server: Now discover services.");
            /* Perform service discovery. */
            rc = ble_peer_disc_all(event->connect.conn_handle,
                               (peer_disc_fn *)ble_on_disc_complete, NULL);
            if (rc != 0)
            {
                ROB_LOGE(ble_server_log_prefix, "Server: Failed to discover services; rc=%d\n", rc);
                return 0;
            }
    
            
        }
        
        if (event->connect.status != 0)
        {
            /* Connection failed; resume advertising. */
            ble_spp_server_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ROB_LOGI(ble_server_log_prefix, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        ROB_LOGI(ble_server_log_prefix, "\n");

        /* Connection terminated; resume advertising. */
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ROB_LOGI(ble_server_log_prefix, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        ROB_LOGI(ble_server_log_prefix, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ROB_LOGI(ble_server_log_prefix, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        ble_spp_server_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ROB_LOGI(ble_server_log_prefix, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    default:
        return 0;
    }
}


void ble_spp_server_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ROB_LOGE(ble_server_log_prefix, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    ROB_LOGI(ble_server_log_prefix, "Device Address: ");
    print_addr(addr_val);
    ROB_LOGI(ble_server_log_prefix, "\n");
    /* Begin advertising. */
    ble_spp_server_advertise();
}

void ble_server_init(char * _log_prefix) {
    ble_server_log_prefix = _log_prefix;
}

#endif
#endif