/*
 * BLE service handling
 * Loosely based on the ESP-IDF-demo
 */
#include <robconfig.h>
#ifdef USE_ESPIDF
#include "ble_spp.h"
#include <host/ble_hs.h>


#include "ble_global.h"
#include "ble_service.h"

#include "../sdp_messaging.h"
#include "../robusto_peer.h"
#include "../sdp_mesh.h"
#include "../sdp_def.h"

/* 16 Bit Alert Notification Service UUID */
#define GATT_SVR_SVC_ALERT_UUID 0x1811

/* 16 Bit Alert Notification Service UUID */
#define BLE_SVC_ANS_UUID16 0x1811

/* 16 Bit Alert Notification Service Characteristic UUIDs */
#define BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT 0x2a47

/* 16 Bit SPP Service UUID */
#define BLE_SVC_SPP_UUID16 0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define BLE_SVC_SPP_CHR_UUID16 0xABF1


/**
 * @brief Callback function for custom service
 *
 */

char* ble_service_log_prefix;

static int ble_handle_incoming(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt) {
    robusto_peer *peer = sdp_mesh_find_peer_by_handle(conn_handle);
    // TODO: This is a weird one, this needs to be set so that 
    // the first reply will not be suppressed (really doesn't matter then). 
    peer->ble_state.initial_media = true;
    return handle_incoming(peer, ctxt->om->om_data, ctxt->om->om_len, SDP_MT_BLE);
}

static int ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            ESP_LOGI(ble_service_log_prefix, "Callback for read");
            break;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            ble_handle_incoming(conn_handle, attr_handle, ctxt);
            break;

        default:
            ESP_LOGI(ble_service_log_prefix, "\nDefault Callback");
            break;
    }
    return 0;
}

/* Define new custom service */
static const struct ble_gatt_svc_def new_ble_svc_gatt_defs[] = {
    {
        /*** Service: GATT */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_ANS_UUID16),
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            /* Support new alert category */
                                            .uuid = BLE_UUID16_DECLARE(BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT),
                                            .access_cb = ble_svc_gatt_handler,
                                            .val_handle = &ble_svc_gatt_read_val_handle,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                                        },
                                        {
                                            0, /* No more characteristics */
                                        }},
    },
    {
        /*** Service: SPP */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_UUID16),
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            /* Support SPP service */
                                            .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_CHR_UUID16),
                                            .access_cb = ble_svc_gatt_handler,
                                            .val_handle = &ble_spp_svc_gatt_read_val_handle,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                                        },
                                        {
                                            0, /* No more characteristics */
                                        }},
    },
    {
        0, /* No more services. */
    },
};

int gatt_svr_register(void)
{
    int rc = 0;

    rc = ble_gatts_count_cfg(new_ble_svc_gatt_defs);

    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(new_ble_svc_gatt_defs);
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}

void ble_init_service(char * _log_prefix) {
    ble_service_log_prefix = _log_prefix;
}
#endif