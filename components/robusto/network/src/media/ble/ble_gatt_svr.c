/* @file ble_gatt_svr.h
 * @brief Gatt server declaration
 *
 *
 * Inspired by the Espressif examples
 * @todo Restructure this into a more understandable solution, perhaps a separate header for peer and gatt stuff.
 */
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#ifdef USE_ESPIDF
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include "ble_spp.h"

/* 16 Bit Alert Notification Service UUID */
#define BLE_SVC_ANS_UUID16 0x1811

/* 16 Bit Alert Notification Service Characteristic UUIDs */
#define BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT 0x2a47
#define BLE_SVC_ANS_CHR_UUID16_NEW_ALERT 0x2a46
#define BLE_SVC_ANS_CHR_UUID16_SUP_UNR_ALERT_CAT 0x2a48
#define BLE_SVC_ANS_CHR_UUID16_UNR_ALERT_STAT 0x2a45
#define BLE_SVC_ANS_CHR_UUID16_ALERT_NOT_CTRL_PT 0x2a44

/**
 * The vendor specific security test service consists of two characteristics:
 *     o random-number-generator: generates a random 32-bit number each time
 *       it is read.  This characteristic can only be read over an encrypted
 *       connection.
 *     o static-value: a single-byte characteristic that can always be read,
 *       but can only be written over an encrypted connection.
 */

/* 59462f12-9543-9999-12c8-58b459a2712d */
static ble_uuid128_t gatt_svr_svc_sec_test_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

/* 5c3a659e-897e-45e1-b016-007107c96df6 */
static ble_uuid128_t gatt_svr_chr_sec_test_rand_uuid =
    BLE_UUID128_INIT(0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
                     0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

/* 5c3a659e-897e-45e1-b016-007107c96df7 */
static ble_uuid128_t gatt_svr_chr_sec_test_static_uuid =
    BLE_UUID128_INIT(0xf7, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
                     0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

static uint8_t gatt_svr_sec_test_static_val;

static int
gatt_svr_chr_access_sec_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

static struct ble_gatt_svc_def gatt_svr_svcs[2];
struct ble_gatt_chr_def chr_def[3];
char *gatt_svr_log_prefix;



ble_uuid128_t * create_mac_on_sec_test_uuid(rob_mac_address *mac_address) {
    ble_uuid128_t *curr_mac_uuid = robusto_malloc(sizeof(ble_uuid128_t));
    curr_mac_uuid->u.type = BLE_UUID_TYPE_128;
    memcpy(curr_mac_uuid->value, gatt_svr_svc_sec_test_uuid.value, sizeof(gatt_svr_svc_sec_test_uuid.value));
    
    memcpy(curr_mac_uuid->value + sizeof(curr_mac_uuid->value) - ROBUSTO_MAC_ADDR_LEN, mac_address, ROBUSTO_MAC_ADDR_LEN);
    return curr_mac_uuid;
}

void init_ble_gatt_svc_def(rob_mac_address *mac_address)
{
    memcpy(gatt_svr_svc_sec_test_uuid.value + sizeof(gatt_svr_chr_sec_test_rand_uuid.value) - ROBUSTO_MAC_ADDR_LEN, mac_address, ROBUSTO_MAC_ADDR_LEN);
    memcpy(gatt_svr_chr_sec_test_rand_uuid.value + sizeof(gatt_svr_chr_sec_test_rand_uuid.value) - ROBUSTO_MAC_ADDR_LEN, mac_address, ROBUSTO_MAC_ADDR_LEN);
    memcpy(gatt_svr_chr_sec_test_static_uuid.value + sizeof(gatt_svr_chr_sec_test_rand_uuid.value) - ROBUSTO_MAC_ADDR_LEN, mac_address, ROBUSTO_MAC_ADDR_LEN);
    /*** Service: Security test. */
   
    memset(&chr_def, 0, sizeof(struct ble_gatt_chr_def) * 3);
    /*** Characteristic: Random number generator. */
    chr_def[0].uuid = &gatt_svr_chr_sec_test_rand_uuid.u;
    chr_def[0].access_cb = gatt_svr_chr_access_sec_test;
    chr_def[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC;

    /*** Characteristic: Static value. */
    chr_def[1].uuid = &gatt_svr_chr_sec_test_static_uuid.u;
    chr_def[1].access_cb = gatt_svr_chr_access_sec_test;
    chr_def[1].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC;
    
    /*** Service: Security test. */
    memset(&gatt_svr_svcs, 0, sizeof(struct ble_gatt_svc_def) *2);
    gatt_svr_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    gatt_svr_svcs[0].uuid = &gatt_svr_svc_sec_test_uuid.u;
    gatt_svr_svcs[0].characteristics = chr_def;
    
}

static int
gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
gatt_svr_chr_access_sec_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    const ble_uuid_t *uuid;
    int rand_num;
    int rc;

    uuid = ctxt->chr->uuid;

    /* Determine which characteristic is being accessed by examining its
     * 128-bit UUID.
     */

    if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_rand_uuid.u) == 0)
    {
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);

        /* Respond with a 32-bit random number. */
        rand_num = rand();
        rc = os_mbuf_append(ctxt->om, &rand_num, sizeof rand_num);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_static_uuid.u) == 0)
    {
        switch (ctxt->op)
        {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            rc = os_mbuf_append(ctxt->om, &gatt_svr_sec_test_static_val,
                                sizeof gatt_svr_sec_test_static_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            rc = gatt_svr_chr_write(ctxt->om,
                                    sizeof gatt_svr_sec_test_static_val,
                                    sizeof gatt_svr_sec_test_static_val,
                                    &gatt_svr_sec_test_static_val, NULL);
            return rc;

        default:
            assert(0);
            return BLE_ATT_ERR_UNLIKELY;
        }
    }

    /* Unknown characteristic; the nimble stack should not have called this
     * function.
     */
    assert(0);
    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int new_gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}

void init_ble_gatt_svr(char *_log_prefix, rob_mac_address *mac_address)
{
    gatt_svr_log_prefix = _log_prefix;
    init_ble_gatt_svc_def(mac_address);

    // TODO: Check out if ESP_ERROR_CHECK could't be used.
    int ret = new_gatt_svr_init();
    assert(ret == 0);
}
#endif
#endif