/**
 * @file ble_spp.h
 * @brief Collection of helpers, types and declarations for a BLE Serial Port Profile (spp service 
 * 
 * 
 * Inspired by the Espressif examples
 * @todo Restructure this into a more understandable solution, perhaps a separate header for peer and gatt stuff.
 */


#ifndef _BLE_SPP_H_
#define _BLE_SPP_H_

#include <stdbool.h>
#include <nimble/ble.h>
#include <modlog/modlog.h>
#include <host/ble_uuid.h>
#include <host/ble_gatt.h>
#include <host/ble_gap.h>


// Client
struct ble_hs_adv_fields;
struct ble_gap_conn_desc;
struct ble_hs_cfg;
union ble_store_value;
union ble_store_key;

//Server
//struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/** GATT server. */
#define GATT_SVR_SVC_ALERT_UUID              0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID  0x2A47
#define GATT_SVR_CHR_NEW_ALERT               0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID  0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID     0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT       0x2A44

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

int new_gatt_svr_init(void);

/* Console - from server */
int scli_init(void);
int scli_receive_key(int *key);

/** Misc. */
void print_bytes(const uint8_t *bytes, int len);
void print_mbuf(const struct os_mbuf *om);
char *addr_str(const void *addr);
void print_uuid(const ble_uuid_t *uuid);
void print_conn_desc(const struct ble_gap_conn_desc *desc);
void print_adv_fields(const struct ble_hs_adv_fields *fields);
void print_addr(const void *addr);
/** Peer. */


struct peer_dsc {
    SLIST_ENTRY(peer_dsc) next;
    struct ble_gatt_dsc dsc;
};
SLIST_HEAD(peer_dsc_list, peer_dsc);

struct peer_chr {
    SLIST_ENTRY(peer_chr) next;
    struct ble_gatt_chr chr;

    struct peer_dsc_list dscs;
};
SLIST_HEAD(peer_chr_list, peer_chr);

struct peer_svc {
    SLIST_ENTRY(peer_svc) next;
    struct ble_gatt_svc svc;

    struct peer_chr_list chrs;
};
SLIST_HEAD(peer_svc_list, peer_svc);

struct ble_peer;
typedef void peer_disc_fn(const struct ble_peer *peer, int status, void *arg);



typedef struct ble_peer {
    SLIST_ENTRY(ble_peer) next;

    uint16_t conn_handle;

    uint16_t sdp_handle;

    /** Connection info */
    struct ble_gap_conn_desc desc;

    /** List of discovered GATT services. */
    struct peer_svc_list svcs;

    int failure_count;

    /** Keeps track of where we are in the service discovery process. */
    uint16_t disc_prev_chr_val;
    struct peer_svc *cur_svc;

    /** Callback that gets executed when service discovery completes. */
    peer_disc_fn *disc_cb;
    void *disc_cb_arg;
} ble_peer;

SLIST_HEAD(, ble_peer) ble_peers;

int ble_peer_disc_all(uint16_t conn_handle, peer_disc_fn *disc_cb,
                  void *disc_cb_arg);
const struct peer_dsc *
ble_peer_dsc_find_uuid(const struct ble_peer *peer, const ble_uuid_t *svc_uuid,
                   const ble_uuid_t *chr_uuid, const ble_uuid_t *dsc_uuid);
const struct peer_chr *
ble_peer_chr_find_uuid(const struct ble_peer *peer, const ble_uuid_t *svc_uuid,
                   const ble_uuid_t *chr_uuid);
const struct peer_svc *
ble_peer_svc_find_uuid(const struct ble_peer *peer, const ble_uuid_t *uuid);
int ble_peer_delete(uint16_t conn_handle);
int ble_peer_add(uint16_t conn_handle, struct ble_gap_conn_desc desc);
int ble_peer_init(char *_log_prefix, int max_peers, int max_svcs, int max_chrs, int max_dscs);

struct ble_peer * ble_peer_find(uint16_t conn_handle);

#endif



