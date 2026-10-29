#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW

#include <robusto_halow.h>
#include <robusto_concurrency.h>
#include <robusto_incoming.h>
#include <robusto_logging.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_queue.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include <stdlib.h>
#include <string.h>
#ifdef CONFIG_ROBUSTO_HALOW_SPI
#include "spi/halow_spi.h"
#endif

#define HALOW_DATA 0U
#define HALOW_RECEIPT_REQUEST 1U
#define HALOW_RECEIPT 2U
#define HALOW_FRAGMENT_SIZE (ROBUSTO_HALOW_TX_PAYLOAD_MAX - 22U)

static char *log_prefix = "HaLow";
static robusto_halow_driver_t driver;
static bool registered;
#ifdef CONFIG_ROBUSTO_HALOW_SPI
static bool spi_registered;
#endif
static bool initialized;
static bool started;
static bool link_ready;
static bool large_worker_active;
static bool servicing_small;
static mutex_ref_t tx_mutex;
static mutex_ref_t receipt_mutex;
static uint32_t next_sequence;
static uint32_t rx_envelopes;
static bool receipt_pending;
static bool receipt_received;
static uint32_t receipt_sequence;
static uint8_t receipt_source[6];
static uint8_t receipt_identity[6];
static queue_context_t queue;
static STAILQ_HEAD(halow_work_queue, media_queue_item) work_queue;

static void put32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint32_t get32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

#ifdef CONFIG_ROBUSTO_HALOW_RX_TRACE
static uint32_t rx_trace_entries;
static uint32_t rx_trace_fragments;

static void trace_receive_entry(const uint8_t source[6], const uint8_t *frame, uint32_t length)
{
    ++rx_trace_entries;
    bool header = frame != NULL && length >= ROBUSTO_HALOW_HEADER_SIZE;
    bool rh = header && frame[0] == 'R' && frame[1] == 'H';
    int context = frame != NULL && length >= ROBUSTO_HALOW_HEADER_SIZE + 5U
        ? frame[ROBUSTO_HALOW_HEADER_SIZE + 4U] : -1;
    int subtype = frame != NULL && length >= ROBUSTO_HALOW_HEADER_SIZE + 6U
        ? frame[ROBUSTO_HALOW_HEADER_SIZE + 5U] : -1;
    if (rx_trace_entries <= 128U || rx_trace_entries % 64U == 0) {
        ROB_LOGI(log_prefix,
                 "RXTRACE HaLow entry=%lu unvalidated=1 bytes=%lu frame_present=%u src_present=%u rh=%u flags=%d seq=%lu declared=%u context=%d subtype=%d",
                 (unsigned long)rx_trace_entries, (unsigned long)length,
                 frame != NULL, source != NULL, rh, header ? frame[3] : -1,
                 (unsigned long)(header ? get32(frame + 4) : 0),
                 header ? (unsigned)frame[14] | ((unsigned)frame[15] << 8) : 0U,
                 context, subtype);
    }
    if (rh && frame[3] <= HALOW_RECEIPT_REQUEST && context >= 0 &&
        (context & 7U) == MSG_FRAGMENTED && (subtype & 0x7f) == FRAG_MESSAGE) {
        ++rx_trace_fragments;
        if (rx_trace_fragments <= 32U || rx_trace_fragments % 64U == 0) {
            uint32_t index_offset = (subtype & 0x80) ? 18U : 6U;
            bool index_present = length >= ROBUSTO_HALOW_HEADER_SIZE + index_offset + 4U;
            ROB_LOGI(log_prefix,
                     "RXTRACE FRAG_MESSAGE entry=%lu seen=%lu unvalidated=1 bytes=%lu seq=%lu hash=%lu index_present=%u index=%lu",
                     (unsigned long)rx_trace_entries, (unsigned long)rx_trace_fragments,
                     (unsigned long)length, (unsigned long)get32(frame + 4),
                     (unsigned long)get32(frame + ROBUSTO_HALOW_HEADER_SIZE),
                     index_present,
                     (unsigned long)(index_present
                         ? get32(frame + ROBUSTO_HALOW_HEADER_SIZE + index_offset) : 0));
        }
    }
}
#endif

static rob_ret_val_t fail(const char *operation, rob_ret_val_t result)
{
    ROB_LOGE(log_prefix, "%s failed: %d", operation, result);
    return result;
}

void robusto_halow_dump_tx_trace(void)
{
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    robusto_halow_spi_dump_tx_trace();
#endif
}

static rob_ret_val_t write_frame(const uint8_t destination[6], uint8_t flags,
                                 uint32_t sequence, const uint8_t *payload,
                                 uint32_t length, bool wait_for_credit)
{
    uint8_t frame[ROBUSTO_HALOW_TX_FRAME_MAX];
    if (length > ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
        return fail("Frame length", ROB_ERR_MESSAGE_TOO_LONG);
    }
    frame[0] = 'R';
    frame[1] = 'H';
    frame[2] = 1;
    frame[3] = flags;
    put32(frame + 4, sequence);
    memcpy(frame + 8, get_host_peer()->base_mac_address, 6);
    frame[14] = (uint8_t)length;
    frame[15] = (uint8_t)(length >> 8);
    if (length != 0) {
        memcpy(frame + ROBUSTO_HALOW_HEADER_SIZE, payload, length);
    }
    uint32_t crc = robusto_crc32(0, frame, 16);
    crc = robusto_crc32(crc, frame + ROBUSTO_HALOW_HEADER_SIZE, length);
    put32(frame + 16, crc);

    uint32_t credit_begin = (uint32_t)r_millis();
    bool waiting = false;
    for (;;) {
        if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
            return fail("Write mutex", ROB_ERR_MUTEX);
        }
        rob_ret_val_t result = ROB_ERR_NOT_READY;
        int written = 0;
        if (started && link_ready) {
            uint32_t frame_length = ROBUSTO_HALOW_HEADER_SIZE + length;
            written = driver.write(driver.context, destination, frame, frame_length);
            result = written == (int)frame_length ? ROB_OK :
                     written == ROB_ERR_QUEUE_FULL ? ROB_ERR_QUEUE_FULL : ROB_ERR_SEND_FAIL;
            if (result != ROB_OK && result != ROB_ERR_QUEUE_FULL) {
                ROB_LOGE(log_prefix, "Short/failed local write: expected=%lu actual=%d",
                         (unsigned long)frame_length, written);
            }
        }
        if (robusto_mutex_give(tx_mutex) != ROB_OK) {
            return fail("Write mutex release", ROB_ERR_MUTEX);
        }
        if (result != ROB_ERR_QUEUE_FULL) {
            if (waiting && result == ROB_OK) {
                ROB_LOGI(log_prefix, "TX credits replenished after %lu ms",
                         (unsigned long)((uint32_t)r_millis() - credit_begin));
            }
            return result == ROB_OK ? ROB_OK : fail("Local write", result);
        }
        if (!wait_for_credit) {
            return result;
        }
        if (!waiting) {
            ROB_LOGW(log_prefix, "TX backpressure: waiting for module credits, bounded to %u ms",
                     CONFIG_ROB_RECEIPT_TIMEOUT_MS);
            waiting = true;
        }
        if ((uint32_t)((uint32_t)r_millis() - credit_begin) >= CONFIG_ROB_RECEIPT_TIMEOUT_MS) {
            return fail("TX credit wait timeout", ROB_ERR_TIMEOUT);
        }
        /* Release both the adapter and SPI locks so RX can refresh credits and send receipts. */
        if (flags == HALOW_DATA) robusto_halow_service_small_queue();
        r_delay(10);
    }
}

static rob_ret_val_t send_packet_internal(robusto_peer_t *peer, uint8_t *data,
                                          uint32_t length, bool receipt, bool wait_for_credit)
{
    if (peer == NULL || data == NULL || length == 0 || !peer->halow_address_valid) {
        return fail("Send arguments/address", ROB_ERR_INVALID_ARG);
    }
    if (length > ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
        return fail("Send length", ROB_ERR_MESSAGE_TOO_LONG);
    }
    if (robusto_mutex_take(receipt_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Receipt mutex", ROB_ERR_MUTEX);
    }
    if (receipt && receipt_pending) {
        if (robusto_mutex_give(receipt_mutex) != ROB_OK) {
            return fail("Concurrent receipt mutex release", ROB_ERR_MUTEX);
        }
        return fail("Concurrent receipt request", ROB_ERR_NOT_READY);
    }
    uint32_t sequence = ++next_sequence;
    if (receipt) {
        receipt_sequence = sequence;
        memcpy(receipt_source, peer->halow_mac_address, 6);
        memcpy(receipt_identity, peer->base_mac_address, 6);
        receipt_received = false;
        receipt_pending = true;
    }
    if (robusto_mutex_give(receipt_mutex) != ROB_OK) {
        return fail("Receipt mutex release", ROB_ERR_MUTEX);
    }
    rob_ret_val_t result = write_frame(peer->halow_mac_address,
                                       receipt ? HALOW_RECEIPT_REQUEST : HALOW_DATA,
                                       sequence, data, length, wait_for_credit);
    if (receipt) {
        uint32_t start = (uint32_t)r_millis();
        for (;;) {
            if (robusto_mutex_take(receipt_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
                return fail("Receipt wait mutex", ROB_ERR_MUTEX);
            }
            bool done = result != ROB_OK || receipt_received ||
                        (uint32_t)((uint32_t)r_millis() - start) >= CONFIG_ROB_RECEIPT_TIMEOUT_MS;
            if (done) {
                if (result == ROB_OK && !receipt_received) {
                    result = ROB_ERR_NO_RECEIPT;
                }
                receipt_pending = false;
            }
            if (robusto_mutex_give(receipt_mutex) != ROB_OK) {
                return fail("Receipt wait mutex release", ROB_ERR_MUTEX);
            }
            if (done) {
                break;
            }
            r_delay(1);
        }
    }
    if (result == ROB_ERR_QUEUE_FULL) return result;
    add_to_history(&peer->halow_info, true, result);
    return result == ROB_OK ? ROB_OK : fail("Packet send/receipt", result);
}

static rob_ret_val_t send_packet(robusto_peer_t *peer, uint8_t *data,
                                 uint32_t length, bool receipt)
{
    robusto_halow_service_small_queue();
    bool fragment_packet = data != NULL && length >= 6U &&
                           (data[ROBUSTO_CRC_LENGTH] & 7U) == MSG_FRAGMENTED;
    return send_packet_internal(peer, data, length, receipt, !fragment_packet);
}

static rob_ret_val_t send_packet_from_rx(robusto_peer_t *peer, uint8_t *data,
                                         uint32_t length, bool receipt)
{
    rob_ret_val_t result = send_packet_internal(peer, data, length, receipt, false);
    if (result == ROB_ERR_QUEUE_FULL) {
        ROB_LOGW(log_prefix, "RX response not submitted: no TX credits; RX remains unblocked");
    }
    return result;
}

static rob_ret_val_t send_message(robusto_peer_t *peer, uint8_t *data,
                                  uint32_t length, bool receipt)
{
    if (data == NULL || length <= ROBUSTO_PREFIX_BYTES) {
        return fail("Message length", ROB_ERR_MESSAGE_TOO_SHORT);
    }
    data += ROBUSTO_PREFIX_BYTES;
    length -= ROBUSTO_PREFIX_BYTES;
    if (length > ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
        return send_message_fragmented(peer, robusto_mt_halow, data, length,
                                       HALOW_FRAGMENT_SIZE, send_packet);
    }
    return send_packet(peer, data, length, receipt);
}

static void *first_item(queue_context_t *context)
{
    (void)context;
    return STAILQ_FIRST(&work_queue);
}

static void *next_item(void *item)
{
    return STAILQ_NEXT((media_queue_item_t *)item, items);
}

static void remove_first(queue_context_t *context)
{
    STAILQ_REMOVE_HEAD(&work_queue, items);
    --context->count;
}

static void remove_item(queue_context_t *context, void *item)
{
    STAILQ_REMOVE(&work_queue, (media_queue_item_t *)item, media_queue_item, items);
    --context->count;
}

static void insert_tail(queue_context_t *context, void *item)
{
    STAILQ_INSERT_TAIL(&work_queue, (media_queue_item_t *)item, items);
    ++context->count;
}

static void insert_head(queue_context_t *context, void *item)
{
    STAILQ_INSERT_HEAD(&work_queue, (media_queue_item_t *)item, items);
    ++context->count;
}

static rob_ret_val_t admit_send(void *owner, uint32_t length, bool before_allocation)
{
    if (length <= ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX) return ROB_OK;
    if (before_allocation && !robusto_send_queue_accepts_large_message(robusto_mt_halow)) {
        return fail("Large send queue preflight", ROB_ERR_QUEUE_FULL);
    }
    return robusto_halow_begin_large_send(owner);
}

static void release_send(void *owner, uint32_t length)
{
    if (length > ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
        robusto_halow_end_large_send(owner);
    }
}

static void do_work(void *item)
{
    media_queue_item_t *work = item;
    robusto_peer_t *peer = work->peer;
    uint8_t presentation_mask = work->presentation_mask;
    bool large = work->data_length > ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX;
    if (large) large_worker_active = true;
    send_work_item(work, &work->peer->halow_info, robusto_mt_halow,
                   send_message, NULL, &queue);
    robusto_halow_end_presentation(peer, presentation_mask);
    if (large) large_worker_active = false;
}

void robusto_halow_service_small_queue(void)
{
    if (!large_worker_active || servicing_small) return;
    if (robusto_mutex_take(queue.__x_queue_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        fail("Small-message service mutex", ROB_ERR_MUTEX);
        return;
    }
    media_queue_item_t *item = NULL;
    if (!queue.blocked) {
        STAILQ_FOREACH(item, &work_queue, items) {
            if (item->data_length <= ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
                remove_item(&queue, item);
                break;
            }
        }
    }
    if (robusto_mutex_give(queue.__x_queue_mutex) != ROB_OK) {
        fail("Small-message service mutex release", ROB_ERR_MUTEX);
        /* Removed work cannot safely be continued or requeued with unknown mutex ownership. */
        abort();
    }
    if (item != NULL) {
        servicing_small = true;
        do_work(item);
        servicing_small = false;
    }
}

queue_context_t *halow_get_queue_context(void)
{
    return initialized ? &queue : NULL;
}

rob_ret_val_t robusto_halow_begin_large_send(robusto_peer_t *peer)
{
    if (peer == NULL) return fail("Large send peer", ROB_ERR_INVALID_ARG);
    if (!initialized) return fail("Large send initialization", ROB_ERR_NOT_READY);
    if (robusto_mutex_take(queue.__x_queue_mutex, 1000) != ROB_OK) {
        return fail("Large send reservation mutex", ROB_ERR_MUTEX);
    }
    rob_ret_val_t result = ROB_OK;
    if (peer->halow_large_send_pending) {
        ROB_LOGW(log_prefix, "Large HaLow message not admitted: peer=%s already has a queued/running transfer",
                 peer->name);
        result = ROB_ERR_QUEUE_FULL;
    } else {
        peer->halow_large_send_pending = true;
    }
    if (robusto_mutex_give(queue.__x_queue_mutex) != ROB_OK) {
        return fail("Large send reservation mutex release", ROB_ERR_MUTEX);
    }
    return result;
}

void robusto_halow_end_large_send(robusto_peer_t *peer)
{
    if (robusto_mutex_take(queue.__x_queue_mutex, 1000) != ROB_OK) {
        fail("Large send completion mutex", ROB_ERR_MUTEX);
        return;
    }
    peer->halow_large_send_pending = false;
    if (robusto_mutex_give(queue.__x_queue_mutex) != ROB_OK) {
        fail("Large send completion mutex release", ROB_ERR_MUTEX);
    }
}

rob_ret_val_t robusto_halow_begin_presentation(robusto_peer_t *peer, uint8_t reason_mask)
{
    if (peer == NULL || reason_mask == 0 || (reason_mask & (reason_mask - 1U)) != 0 ||
        reason_mask >= (1U << (presentation_reply + 1U))) {
        return fail("Invalid presentation reservation", ROB_ERR_INVALID_ARG);
    }
    if (!initialized) {
        return fail("Presentation queue not initialized", ROB_ERR_NOT_READY);
    }
    if (robusto_mutex_take(queue.__x_queue_mutex, 10000) != ROB_OK) {
        return fail("Presentation reservation mutex", ROB_ERR_MUTEX);
    }
    rob_ret_val_t result = ROB_OK;
    if (peer->halow_pending_presentations & reason_mask) {
        ROB_LOGW(log_prefix, "HI already pending for peer %s (reason mask=0x%02x)",
                 peer->name, reason_mask);
        result = ROB_ERR_NOT_READY;
    } else {
        peer->halow_pending_presentations |= reason_mask;
    }
    robusto_mutex_give(queue.__x_queue_mutex);
    return result;
}

void robusto_halow_end_presentation(robusto_peer_t *peer, uint8_t reason_mask)
{
    if (reason_mask == 0) {
        return;
    }
    if (robusto_mutex_take(queue.__x_queue_mutex, 10000) != ROB_OK) {
        fail("Presentation completion mutex", ROB_ERR_MUTEX);
        return;
    }
    peer->halow_pending_presentations &= (uint8_t)~reason_mask;
    robusto_mutex_give(queue.__x_queue_mutex);
}

void halow_peer_init_peer(robusto_peer_t *peer)
{
    memset(&peer->halow_info, 0, sizeof(peer->halow_info));
    peer_stat_reset(&peer->halow_info);
    set_state(peer, &peer->halow_info, robusto_mt_halow,
               media_state_initiating, media_problem_none);
}

rob_ret_val_t robusto_halow_register_driver(const robusto_halow_driver_t *binding)
{
    if (binding == NULL || binding->start == NULL || binding->stop == NULL ||
        binding->write == NULL || registered || initialized) {
        return fail("Driver registration", ROB_ERR_INVALID_ARG);
    }
    driver = *binding;
    registered = true;
    return ROB_OK;
}

rob_ret_val_t robusto_halow_init(char *prefix)
{
    if (prefix != NULL) {
        log_prefix = prefix;
    }
    if (initialized) {
        return fail("Initialization/driver binding", ROB_ERR_NOT_READY);
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI
    if (!spi_registered) {
        if (registered) {
            return fail("Configured SPI/custom driver conflict", ROB_ERR_INVALID_ARG);
        }
        rob_ret_val_t binding_result = robusto_halow_spi_register();
        if (binding_result != ROB_OK) {
            return fail("SPI driver registration", binding_result);
        }
        spi_registered = true;
    }
#endif
    if (!registered) {
        return fail("Initialization/driver binding", ROB_ERR_NOT_READY);
    }
    tx_mutex = robusto_mutex_init();
    receipt_mutex = robusto_mutex_init();
    if (tx_mutex == NULL || receipt_mutex == NULL) {
        if (tx_mutex != NULL) robusto_mutex_deinit(tx_mutex);
        if (receipt_mutex != NULL) robusto_mutex_deinit(receipt_mutex);
        return fail("Mutex initialization", ROB_ERR_MUTEX);
    }
    STAILQ_INIT(&work_queue);
    memset(&queue, 0, sizeof(queue));
    queue.work_queue = &work_queue;
    queue.first_queue_item_cb = first_item;
    queue.next_queueitem_cb = next_item;
    queue.remove_first_queueitem_cb = remove_first;
    queue.remove_queueitem_cb = remove_item;
    queue.insert_tail_cb = insert_tail;
    queue.insert_head_cb = insert_head;
    queue.on_work_cb = do_work;
    queue.on_admit_cb = admit_send;
    queue.on_release_cb = release_send;
    queue.max_task_count = 1;
    queue.normal_max_count = 3;
    queue.important_max_count = 5;
    queue.blocked = true;
    queue.watchdog_timeout = CONFIG_ROB_RECEIPT_TIMEOUT_MS;
    rob_ret_val_t result = init_work_queue(&queue, prefix, "HaLow Queue");
    if (result != ROB_OK) {
        robusto_mutex_deinit(tx_mutex);
        robusto_mutex_deinit(receipt_mutex);
        return fail("Queue initialization", result);
    }
    initialized = true;
    add_host_supported_media_type(robusto_mt_halow);
    return ROB_OK;
}

rob_ret_val_t robusto_halow_start(void)
{
    if (!initialized) {
        return fail("Start state", ROB_ERR_NOT_READY);
    }
    if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Start mutex", ROB_ERR_MUTEX);
    }
    if (started) {
        if (robusto_mutex_give(tx_mutex) != ROB_OK) {
            return fail("Already-started mutex release", ROB_ERR_MUTEX);
        }
        return fail("Already started", ROB_ERR_NOT_READY);
    }
    started = true;
    if (robusto_mutex_give(tx_mutex) != ROB_OK) {
        return fail("Start mutex release", ROB_ERR_MUTEX);
    }
    rob_ret_val_t result = driver.start(driver.context);
    if (result != ROB_OK) {
        rob_ret_val_t link_result = robusto_halow_set_link_ready(false);
        if (link_result != ROB_OK) {
            return link_result;
        }
        if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
            return fail("Failed-start mutex", ROB_ERR_MUTEX);
        }
        started = false;
        if (robusto_mutex_give(tx_mutex) != ROB_OK) {
            return fail("Failed-start mutex release", ROB_ERR_MUTEX);
        }
        return fail("Driver start", result);
    }
    return ROB_OK;
}

rob_ret_val_t robusto_halow_reset_and_reinitialize(void)
{
    if (!initialized) {
        return fail("Reset state", ROB_ERR_NOT_READY);
    }
    if (driver.reset_and_reinitialize == NULL) {
        return fail("Module reset unsupported", ROB_ERR_NOT_SUPPORTED);
    }
    rob_ret_val_t result = driver.reset_and_reinitialize(driver.context);
    return result == ROB_OK ? ROB_OK : fail("Module reset/reinitialization", result);
}

rob_ret_val_t robusto_halow_set_link_ready(bool ready)
{
    if (!initialized) {
        return fail("Link state", ROB_ERR_NOT_READY);
    }
    if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Link mutex", ROB_ERR_MUTEX);
    }
    if (ready && !started) {
        if (robusto_mutex_give(tx_mutex) != ROB_OK) {
            return fail("Stopped-link mutex release", ROB_ERR_MUTEX);
        }
        return fail("Link not started", ROB_ERR_NOT_READY);
    }
    link_ready = ready;
    if (robusto_mutex_give(tx_mutex) != ROB_OK) {
        return fail("Link mutex release", ROB_ERR_MUTEX);
    }
    set_queue_blocked(&queue, !ready);
    ROB_LOGI(log_prefix, "HaLow link ready=%u", ready ? 1U : 0U);
    return ROB_OK;
}

rob_ret_val_t robusto_halow_stop(void)
{
    if (!initialized) {
        return fail("Stop state", ROB_ERR_NOT_READY);
    }
    if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Stop-state mutex", ROB_ERR_MUTEX);
    }
    bool was_started = started;
    if (robusto_mutex_give(tx_mutex) != ROB_OK) {
        return fail("Stop-state mutex release", ROB_ERR_MUTEX);
    }
    if (!was_started) {
        return fail("Already stopped", ROB_ERR_NOT_READY);
    }
    rob_ret_val_t result = robusto_halow_set_link_ready(false);
    if (result != ROB_OK) {
        return result;
    }
    result = driver.stop(driver.context);
    if (result != ROB_OK) {
        return fail("Driver stop", result);
    }
    if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Stop mutex", ROB_ERR_MUTEX);
    }
    started = false;
    link_ready = false;
    if (robusto_mutex_give(tx_mutex) != ROB_OK) {
        return fail("Stop mutex release", ROB_ERR_MUTEX);
    }
    set_queue_blocked(&queue, true);
    return ROB_OK;
}

rob_ret_val_t robusto_halow_bind_peer(robusto_peer_t *peer, const uint8_t radio_mac[6])
{
    if (peer == NULL || radio_mac == NULL || (radio_mac[0] & 1U) ||
        memcmp(radio_mac, "\0\0\0\0\0\0", 6) == 0) {
        return fail("Peer binding", ROB_ERR_INVALID_ARG);
    }
    bool changed = !peer->halow_address_valid ||
                   memcmp(peer->halow_mac_address, radio_mac, 6) != 0 ||
                   !(peer->supported_media_types & robusto_mt_halow);
    memcpy(peer->halow_mac_address, radio_mac, 6);
    peer->halow_address_valid = true;
    if (!(peer->supported_media_types & robusto_mt_halow)) {
        peer->supported_media_types |= robusto_mt_halow;
        halow_peer_init_peer(peer);
    }
    if (changed) {
        robusto_peer_update_relation(peer);
    }
    return ROB_OK;
}

rob_ret_val_t robusto_halow_get_rx_stats(robusto_halow_rx_stats_t *out)
{
    if (out == NULL) return fail("RX statistics arguments", ROB_ERR_INVALID_ARG);
    memset(out, 0, sizeof(*out));
    if (!initialized) return fail("RX statistics state", ROB_ERR_NOT_READY);
    rob_ret_val_t result = driver.get_rx_stats != NULL
        ? driver.get_rx_stats(driver.context, out) : ROB_ERR_NOT_SUPPORTED;
    out->envelopes = rx_envelopes;
    return result;
}

rob_ret_val_t robusto_halow_receive(const uint8_t source[6], const uint8_t *frame,
                                   uint32_t length, const int16_t *rssi_dbm)
{
#ifdef CONFIG_ROBUSTO_HALOW_RX_TRACE
    trace_receive_entry(source, frame, length);
#endif
    if (!initialized) {
        return fail("Receive state", ROB_ERR_NOT_READY);
    }
    if (robusto_mutex_take(tx_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
        return fail("Receive-state mutex", ROB_ERR_MUTEX);
    }
    bool ready = started && link_ready;
    if (robusto_mutex_give(tx_mutex) != ROB_OK) {
        return fail("Receive-state mutex release", ROB_ERR_MUTEX);
    }
    if (!ready) {
        return fail("Receive link", ROB_ERR_NOT_READY);
    }
    if (source == NULL || frame == NULL || length < ROBUSTO_HALOW_HEADER_SIZE ||
        length > ROBUSTO_HALOW_FRAME_MAX || (source[0] & 1U) ||
        memcmp(source, "\0\0\0\0\0\0", 6) == 0) {
        return fail("Receive frame", ROB_ERR_INVALID_ARG);
    }
    uint8_t flags = frame[3];
    uint32_t payload_length = (uint32_t)frame[14] | ((uint32_t)frame[15] << 8);
    if (frame[0] != 'R' || frame[1] != 'H' || frame[2] != 1 ||
        flags > HALOW_RECEIPT ||
        (frame[8] & 1U) || memcmp(frame + 8, "\0\0\0\0\0\0", 6) == 0 ||
        length != ROBUSTO_HALOW_HEADER_SIZE + payload_length ||
        (flags == HALOW_RECEIPT && payload_length != 0)) {
        return fail("Receive envelope", ROB_ERR_PARSING_FAILED);
    }
    const uint8_t *payload = frame + ROBUSTO_HALOW_HEADER_SIZE;
    uint32_t crc = robusto_crc32(0, frame, 16);
    crc = robusto_crc32(crc, payload, payload_length);
    if (crc != get32(frame + 16)) {
        return fail("Receive CRC", ROB_ERR_WRONG_CRC);
    }
    ++rx_envelopes;
    if (rx_envelopes == 1 || rx_envelopes % 64U == 0) {
        ROB_LOGI(log_prefix, "RX envelope validated count=%lu flags=%u payload=%lu",
                 (unsigned long)rx_envelopes, flags, (unsigned long)payload_length);
    }
    uint32_t sequence = get32(frame + 4);
    if (flags == HALOW_RECEIPT) {
        if (robusto_mutex_take(receipt_mutex, CONFIG_ROB_RECEIPT_TIMEOUT_MS) != ROB_OK) {
            return fail("Incoming receipt mutex", ROB_ERR_MUTEX);
        }
        bool matches = receipt_pending && sequence == receipt_sequence &&
                       memcmp(source, receipt_source, 6) == 0 &&
                       memcmp(frame + 8, receipt_identity, 6) == 0;
        if (matches) {
            receipt_received = true;
        }
        if (robusto_mutex_give(receipt_mutex) != ROB_OK) {
            return fail("Incoming receipt mutex release", ROB_ERR_MUTEX);
        }
        return matches ? ROB_OK : fail("Unmatched receipt", ROB_ERR_MESSAGE_FILTERED);
    }
    if (payload_length < ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN) {
        return fail("Receive payload", ROB_ERR_MESSAGE_TOO_SHORT);
    }
    uint8_t context = payload[ROBUSTO_CRC_LENGTH];
    if (context == HEARTBEAT_CONTEXT && payload_length < 7U) {
        return fail("Receive heartbeat", ROB_ERR_MESSAGE_TOO_SHORT);
    }
    if ((context & 7U) == MSG_FRAGMENTED) {
        if (payload_length < 6U) {
            return fail("Receive fragment header", ROB_ERR_MESSAGE_TOO_SHORT);
        }
        uint32_t minimum;
        if (!(payload[5] & 0x80U)) {
            return fail("HaLow fragmentation v2 required; update both peers", ROB_ERR_PARSING_FAILED);
        }
        switch (payload[5] & 0x7fU) {
        case FRAG_REQUEST: minimum = 22U; break;
        case FRAG_MESSAGE: minimum = 10U; break;
        case FRAG_RESULT: minimum = 8U; break;
        case FRAG_RESEND:
        case FRAG_CHECK: minimum = 6U; break;
        default:
            return fail("Receive fragment subtype", ROB_ERR_PARSING_FAILED);
        }
        if (payload_length < minimum + 12U) {
            return fail("Receive fragment body", ROB_ERR_MESSAGE_TOO_SHORT);
        }
        uint8_t subtype = payload[5] & 0x7fU;
        uint32_t round = get32(payload + 14);
        if ((get32(payload + 6) | get32(payload + 10)) == 0 ||
            ((subtype == FRAG_REQUEST || subtype == FRAG_CHECK || subtype == FRAG_RESULT) &&
             payload_length != minimum + 12U) ||
            (subtype == FRAG_REQUEST && round != 0) ||
            ((subtype == FRAG_CHECK || subtype == FRAG_RESEND) && round == 0) ||
            (subtype == FRAG_RESEND && payload_length == 18U)) {
            return fail("Receive HaLow v2 fragment identity/round/shape", ROB_ERR_PARSING_FAILED);
        }
    }
    rob_mac_address identity;
    memcpy(identity, frame + 8, sizeof(identity));
    robusto_peer_t *peer = robusto_peers_find_peer_by_base_mac_address(&identity);
    if (peer == NULL) {
        if (payload[ROBUSTO_CRC_LENGTH] != 0x42) {
            return fail("Unknown peer without presentation", ROB_ERR_PEER_NOT_FOUND);
        }
        peer = robusto_add_init_new_peer(NULL, &identity, robusto_mt_halow);
        if (peer == NULL) {
            return fail("Incoming peer allocation", ROB_ERR_OUT_OF_MEMORY);
        }
    }
    rob_ret_val_t result = robusto_halow_bind_peer(peer, source);
    if (result != ROB_OK) {
        return result;
    }
    uint8_t *copy = robusto_malloc(payload_length);
    if (copy == NULL) {
        return fail("Receive allocation", ROB_ERR_OUT_OF_MEMORY);
    }
    memcpy(copy, payload, payload_length);
    if (flags == HALOW_RECEIPT_REQUEST) {
        result = write_frame(source, HALOW_RECEIPT, sequence, NULL, 0, false);
        if (result != ROB_OK) {
            robusto_free(copy);
            return result;
        }
    }
    peer->halow_info.last_receive = r_millis();
    if (rssi_dbm != NULL) {
        peer->halow_info.latest_rssi_dbm = *rssi_dbm;
        peer->halow_info.latest_rssi_valid = true;
    }
    if (context == HEARTBEAT_CONTEXT) {
        peer->halow_info.last_peer_receive = parse_heartbeat(
            payload, ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
        robusto_free(copy);
        add_to_history(&peer->halow_info, false, ROB_OK);
        return ROB_OK;
    }
    if ((context & 7U) == MSG_FRAGMENTED) {
        handle_fragmented(peer, robusto_mt_halow, copy, payload_length,
                           ROBUSTO_HALOW_PAYLOAD_MAX - 22U, send_packet_from_rx);
        return ROB_OK;
    }
    result = robusto_handle_incoming(copy, payload_length, peer, robusto_mt_halow, 0);
    add_to_history(&peer->halow_info, false, result);
    return result == ROB_OK ? ROB_OK : fail("Incoming message", result);
}

#endif
