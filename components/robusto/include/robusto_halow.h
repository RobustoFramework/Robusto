#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <robusto_retval.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robusto_peer robusto_peer_t;
typedef struct queue_context queue_context_t;

/* The bundled SPI writer handles 1664 bytes, including 22 bytes of vendor headers. */
#define ROBUSTO_HALOW_FRAME_MAX 1642U
#define ROBUSTO_HALOW_HEADER_SIZE 20U
#define ROBUSTO_HALOW_PAYLOAD_MAX (ROBUSTO_HALOW_FRAME_MAX - ROBUSTO_HALOW_HEADER_SIZE)

typedef struct {
    void *context;
    rob_ret_val_t (*start)(void *context);
    rob_ret_val_t (*stop)(void *context);
    /*
     * Synchronously consume/copy the frame. Return its full length, a short
     * count, or a negative error. Exclude vendor headers from the count.
     * This is local submission, not radio delivery. Do not call receive here.
     */
    int (*write)(void *context, const uint8_t destination[6],
                 const uint8_t *frame, uint32_t length);
    /*
     * Optional, serialized lifecycle operation. Quiesce traffic, request one
     * module reboot, and reinitialize once. Use the public stop/start functions
     * to keep the medium's lifecycle synchronized. Must support failed startup.
     * Do not call the public reset function recursively or retry automatically.
     */
    rob_ret_val_t (*reset_and_reinitialize)(void *context);
} robusto_halow_driver_t;

/*
 * Register once, before network initialization; callbacks/context remain valid.
 * Serialize registration, initialization, start and stop calls.
 */
rob_ret_val_t robusto_halow_register_driver(const robusto_halow_driver_t *driver);
rob_ret_val_t robusto_halow_init(char *log_prefix);
rob_ret_val_t robusto_halow_start(void);
rob_ret_val_t robusto_halow_stop(void);

/*
 * Explicit module reset, not host reset or factory defaults. Requires initialized
 * Robusto, not successful backend startup. Serialize with start/stop and other
 * reset calls; application-task context only. Success means validated startup,
 * not independently confirmed reboot or association. Unsupported drivers fail.
 */
rob_ret_val_t robusto_halow_reset_and_reinitialize(void);

/*
 * Backend calls from a task, not an ISR. Receive must run independently of the
 * send worker so receipts and fragment results can arrive while sends wait.
 * Serialize receive calls. Stop must quiesce callbacks before returning.
 */
rob_ret_val_t robusto_halow_set_link_ready(bool ready);
rob_ret_val_t robusto_halow_receive(const uint8_t source[6], const uint8_t *frame,
                                   uint32_t length, const int16_t *rssi_dbm);
rob_ret_val_t robusto_halow_bind_peer(robusto_peer_t *peer, const uint8_t radio_mac[6]);

queue_context_t *halow_get_queue_context(void);
void halow_peer_init_peer(robusto_peer_t *peer);

#ifdef __cplusplus
}
#endif
