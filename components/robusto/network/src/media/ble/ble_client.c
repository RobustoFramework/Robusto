
/***********************
 * This is general client level handling
 ***********************/
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#ifdef USE_ESPIDF

#ifndef CONFIG_BT_NIMBLE_ENABLED 
    #error "Robusto requires NimBLE -BLE only to be enabled in menuconfig."
#endif

#include <host/util/util.h>
#include <host/ble_gap.h>

#include "ble_spp.h"


#include "ble_global.h"

#include "ble_service.h"
#include "ble_client.h"

char * ble_client_log_prefix;

static int ble_spp_client_gap_event(struct ble_gap_event *event, void *arg);

/**
 * Initiates the GAP general discovery procedure.
 */
void ble_spp_client_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ROB_LOGE(ble_client_log_prefix, "Error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      ble_spp_client_gap_event, NULL);
    if (rc != 0)
    {
        ROB_LOGE(ble_client_log_prefix, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}




/**
 * Indicates whether we should try to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int
ble_spp_client_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int i;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {

        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0)
    {
        return rc;
    }

    /* The device has to advertise support for the Alert Notification
     * service (0x1811).
     */
    for (i = 0; i < fields.num_uuids16; i++)
    {
        if (ble_uuid_u16(&fields.uuids16[i].u) == GATT_SVR_SVC_ALERT_UUID)
        {
            return 1;
        }
    }

    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void
ble_spp_client_connect_if_interesting(const struct ble_gap_disc_desc *disc)
{
    uint8_t own_addr_type;
    int rc;

    /* Don't do anything if we don't care about this advertiser. */
    if (!ble_spp_client_should_connect(disc))
    {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0)
    {
        MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ROB_LOGE(ble_client_log_prefix, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */

    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         ble_spp_client_gap_event, NULL);

    switch (rc) {
        case 0:            
            return;
        case BLE_HS_EALREADY:
            ROB_LOGW(ble_client_log_prefix, "Failed connecting, simultaneous attempt in progress - addr_type=%d "
                           "addr=%s", disc->addr.type, addr_str(disc->addr.val));
            return;
        case BLE_HS_EBUSY:
            ROB_LOGW(ble_client_log_prefix, "Peer is busy scanning - addr_type=%d "
                           "addr=%s", disc->addr.type, addr_str(disc->addr.val));
            return;
        case BLE_HS_EDONE:
            ROB_LOGW(ble_client_log_prefix, "Peer is already connected - addr_type=%d "
                           "addr=%s", disc->addr.type, addr_str(disc->addr.val));
            return;
        default:
            ROB_LOGE(ble_client_log_prefix, "Unspecified error occure connecting to peer - addr_type=%d "
                           "addr=%s, rc=%d", disc->addr.type, addr_str(disc->addr.val), rc);
            return;


    }   

}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  ble_spp_client uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_spp_client.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
ble_spp_client_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type)
    {

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ROB_LOGI(ble_client_log_prefix, "connection %s; status=%d; conn_handle=%u ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status, event->connect.conn_handle);        
        if (event->connect.status == 0)
        {
            /* Connection successfully established. */

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            ROB_LOGI(ble_client_log_prefix, "\n");

            rc = ble_negotiate_mtu(event->connect.conn_handle);
            if (rc != 0)
            {
                ROB_LOGE(ble_client_log_prefix, "Failed to negotiate MTU; rc=%d\n", rc);
                return 0;
            }
            int rc = ble_peer_add(event->connect.conn_handle, desc);
            if (rc != 0)
            {
                if (rc == BLE_HS_EALREADY) {
                    ROB_LOGI(ble_client_log_prefix, "Peer was already present (conn_handle=%i).", event->connect.conn_handle);

                } else {
                    ROB_LOGE(ble_client_log_prefix, "Failed to add peer; rc=%d\n", rc);
                    
                }

            }
            ROB_LOGI(ble_client_log_prefix, "Client: Now discover services.");
            /* Perform service discovery. */
            rc = ble_peer_disc_all(event->connect.conn_handle,
                               ble_on_disc_complete, NULL);
            if (rc != 0)
            {
                ROB_LOGE(ble_client_log_prefix, "Client: Failed to discover services; rc=%d\n", rc);
                return 0;
            }
        }
        else
        {
            /* Connection attempt failed; resume scanning. */
            ROB_LOGE(ble_client_log_prefix, "Error: Connection failed; status=%d\n",
                        event->connect.status);
            ble_spp_client_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        ROB_LOGI(ble_client_log_prefix, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        ROB_LOGI(ble_client_log_prefix, "\n");

        /* Forget about peer. */
        ble_peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        ble_spp_client_scan();
        return 0;

    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0)
        {
            return 0;
        }

        /* An advertisment report was received during GAP discovery. */
        print_adv_fields(&fields);

        /* Try to connect to the advertiser if it looks interesting. */
        ble_spp_client_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ROB_LOGI(ble_client_log_prefix, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        ROB_LOGI(ble_client_log_prefix, "received %s; conn_handle=%d attr_handle=%d "
                          "attr_len=%d\n",
                    event->notify_rx.indication ? "indication" : "notification",
                    event->notify_rx.conn_handle,
                    event->notify_rx.attr_handle,
                    OS_MBUF_PKTLEN(event->notify_rx.om));

        ROB_LOGI(ble_client_log_prefix, "Data:\n%s", (char *)(event->notify_rx.om->om_data));

        /* Attribute data is contained in event->notify_rx.om. Use
         * `os_mbuf_copydata` to copy the data received in notification mbuf */
        return 0;

    case BLE_GAP_EVENT_MTU:
        ROB_LOGI(ble_client_log_prefix, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    default:

        return 0;
    }
}


void ble_spp_client_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

   /* Begin scanning for a peripheral to connect to. */
    ble_spp_client_scan();
}

void ble_spp_client_init(char * _log_prefix) {
    ble_client_log_prefix = _log_prefix;
}

#endif
#endif