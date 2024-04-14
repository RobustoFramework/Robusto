/*
 * BLE service handling
 * Loosely based on the ESP-IDF-demo
 */
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#ifdef USE_ESPIDF
#include "ble_spp.h"
#include <host/ble_hs.h>


#include "ble_global.h"
#include "ble_service.h"

#include <robusto_incoming.h>
#include <robusto_peer.h>


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

uint16_t ble_svc_gatt_read_val_handle;
uint16_t ble_spp_svc_gatt_read_val_handle;

char* ble_service_log_prefix;
uint16_t get_ble_spp_svc_gatt_read_val_handle() {
    return ble_spp_svc_gatt_read_val_handle;
};

/**
 * @brief Callback function for custom service
 *
 */



static int ble_handle_incoming(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt) {
    robusto_peer_t *peer = robusto_peers_find_peer_by_handle(conn_handle);
    // TODO: This is a weird one, this needs to be set so that 
    // the first reply will not be suppressed (really doesn't matter then). 

    return robusto_handle_incoming(ctxt->om->om_data, ctxt->om->om_len, peer, robusto_mt_ble, 0);
}

static int ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            ROB_LOGI(ble_service_log_prefix, "Callback for read");
            break;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            ble_handle_incoming(conn_handle, attr_handle, ctxt);
            break;

        default:
            ROB_LOGI(ble_service_log_prefix, "\nDefault Callback");
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
#endif