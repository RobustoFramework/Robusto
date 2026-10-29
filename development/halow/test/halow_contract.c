#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <robusto_halow.h>
#include <robusto_incoming.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_queue.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include "compat_crc32.h"

static robusto_peer_t host;
static robusto_peer_t remote;
static uint8_t radio_mac[6] = {2, 1, 2, 3, 4, 5};
static uint8_t written[ROBUSTO_HALOW_FRAME_MAX];
static uint32_t written_length;
static unsigned writes;
static unsigned received_messages;
static unsigned received_fragments;
static unsigned fragmented_sends;
static unsigned logs;
static unsigned long now;
static int short_write;
static bool return_receipt;
static unsigned bad_receipt;
static unsigned stops;
static unsigned relation_updates;
static unsigned peer_creations;
static unsigned received_heartbeats;
static rob_ret_val_t start_result = ROB_OK;
static rob_ret_val_t stop_result = ROB_OK;
static rob_ret_val_t work_result;
static bool free_work_item;
static bool reject_queue_lock;
static unsigned busy_writes, credit_delays, held_locks;
static bool always_busy, check_credit_unlock, lose_link_on_delay, receipt_on_write;
static bool rx_fragment_backpressure;
static bool compare_credit_frame, check_large_owned;
static uint8_t credit_frame[ROBUSTO_HALOW_FRAME_MAX];
static uint32_t credit_frame_length;
static rob_ret_val_t fragmented_result = ROB_OK;
static bool exercise_small_service;
static media_queue_item_t *first_small, *second_small;
static unsigned small_completed;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
static unsigned trace_dumps;
void robusto_halow_spi_dump_tx_trace(void)
{
    assert(held_locks == 0);
    ++trace_dumps;
}
#endif
#ifdef ROBUSTO_HALOW_TEST_WITH_RESET
static unsigned resets;
static rob_ret_val_t reset_result;
static rob_ret_val_t reset_driver(void *context)
{
    assert(context == NULL);
    ++resets;
    return reset_result;
}
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI
static const robusto_halow_driver_t *spi_binding;
static unsigned spi_registrations;
static rob_ret_val_t spi_registration_result;
rob_ret_val_t robusto_halow_spi_register(void)
{
    ++spi_registrations;
    return spi_registration_result != ROB_OK ? spi_registration_result
                                             : robusto_halow_register_driver(spi_binding);
}
#endif

uint32_t robusto_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    return crc32_be_compat(crc, data, length);
}

static void seal(uint8_t *frame, uint32_t length)
{
    uint32_t crc = robusto_crc32(0, frame, 16);
    crc = robusto_crc32(crc, frame + ROBUSTO_HALOW_HEADER_SIZE,
                        length - ROBUSTO_HALOW_HEADER_SIZE);
    for (unsigned i = 0; i < 4; ++i) frame[16 + i] = (uint8_t)(crc >> (i * 8));
}

void *robusto_malloc(size_t size) { return malloc(size); }
void robusto_free(void *data) { free(data); }
bool robusto_send_queue_accepts_large_message(e_media_type medium)
{
    assert(medium == robusto_mt_halow);
    queue_context_t *queue = halow_get_queue_context();
    return queue != NULL && !queue->blocked && !queue->shutdown &&
           queue->count < queue->normal_max_count;
}
unsigned long r_millis(void) { return now; }
unsigned long r_micros(void) { return now * 1000; }
void r_delay(unsigned long delay)
{
    now += delay;
    if (check_credit_unlock) {
        assert(held_locks == 0);
        ++credit_delays;
        if (lose_link_on_delay) {
            lose_link_on_delay = false;
            assert(robusto_halow_set_link_ready(false) == ROB_OK);
        }
    }
    if (return_receipt) {
        uint8_t ack[ROBUSTO_HALOW_HEADER_SIZE];
        uint8_t source[6];
        memcpy(source, radio_mac, sizeof(source));
        memcpy(ack, written, sizeof(ack));
        ack[3] = 2;
        if (bad_receipt == 1) ack[4] ^= 1;
        memcpy(ack + 8, remote.base_mac_address, 6);
        if (bad_receipt == 2) source[5] ^= 1;
        if (bad_receipt == 3) ack[13] ^= 1;
        ack[14] = ack[15] = 0;
        seal(ack, sizeof(ack));
        return_receipt = false;
        rob_ret_val_t result = robusto_halow_receive(source, ack, sizeof(ack), NULL);
        assert(result == (bad_receipt ? ROB_ERR_MESSAGE_FILTERED : ROB_OK));
    }
}

void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...)
{
    (void)tag;
    (void)level;
    assert(strstr(format, "HaLow fragment local write complete") == NULL);
    ++logs;
}

mutex_ref_t robusto_mutex_init(void) { return calloc(1, 1); }
void robusto_mutex_deinit(mutex_ref_t mutex) { free(mutex); }
rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout)
{
    (void)timeout;
    assert(mutex != NULL);
    if (reject_queue_lock && halow_get_queue_context() != NULL &&
        mutex == halow_get_queue_context()->__x_queue_mutex) return ROB_ERR_MUTEX;
    assert(*(uint8_t *)mutex == 0);
    *(uint8_t *)mutex = 1;
    ++held_locks;
    return ROB_OK;
}
rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex)
{
    assert(mutex != NULL);
    assert(*(uint8_t *)mutex == 1);
    *(uint8_t *)mutex = 0;
    assert(held_locks > 0);
    --held_locks;
    return ROB_OK;
}

rob_ret_val_t init_work_queue(queue_context_t *queue, const char *prefix, const char *name)
{
    (void)prefix; (void)name;
    assert(queue->max_task_count == 1);
    assert(queue->normal_max_count == 3);
    queue->__x_queue_mutex = robusto_mutex_init();
    return ROB_OK;
}
void set_queue_blocked(queue_context_t *queue, bool blocked) { queue->blocked = blocked; }
void peer_stat_reset(robusto_media_t *info) { memset(info, 0, sizeof(*info)); }
void set_state(robusto_peer_t *peer, robusto_media_t *info, e_media_type medium,
                e_media_state state, e_media_problem problem)
{
    (void)peer;
    assert(medium == robusto_mt_halow);
    info->state = state;
    info->problem = problem;
}
void add_to_history(robusto_media_t *info, bool sending, rob_ret_val_t result)
{
    (void)info; (void)sending; (void)result;
}
robusto_peer_t *get_host_peer(void) { return &host; }
void add_host_supported_media_type(e_media_type medium) { host.supported_media_types |= medium; }
robusto_peer_t *robusto_peers_find_peer_by_base_mac_address(rob_mac_address *mac)
{
    return memcmp(*mac, remote.base_mac_address, 6) == 0 ? &remote : NULL;
}
void robusto_peer_update_relation(robusto_peer_t *peer)
{
    assert(peer == &remote && peer->halow_address_valid);
    ++relation_updates;
}
robusto_peer_t *robusto_add_init_new_peer(const char *name, rob_mac_address *mac, robusto_media_types media)
{
    (void)name;
    memcpy(remote.base_mac_address, *mac, 6);
    remote.supported_media_types = media;
    remote.halow_address_valid = false;
    halow_peer_init_peer(&remote);
    ++peer_creations;
    return &remote;
}
uint64_t parse_heartbeat(const uint8_t *data, uint8_t preamble_length)
{
    (void)data;
    assert(preamble_length == ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
    ++received_heartbeats;
    return 123;
}
rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t length,
                                      robusto_peer_t *peer, e_media_type medium, int offset)
{
    assert(length >= 5 && peer == &remote && medium == robusto_mt_halow && offset == 0);
    ++received_messages;
    free(data);
    return ROB_OK;
}
bool handle_fragmented(robusto_peer_t *peer, e_media_type medium, uint8_t *data,
                       int length, uint32_t fragment_size, cb_send_message *send)
{
    assert(peer == &remote && medium == robusto_mt_halow && length >= 6);
    assert(fragment_size == ROBUSTO_HALOW_PAYLOAD_MAX - 22);
    assert(send != NULL);
    if (rx_fragment_backpressure) {
        unsigned long before = now;
        uint8_t response[6] = {0};
        assert(send(peer, response, sizeof(response), false) == ROB_ERR_QUEUE_FULL);
        assert(now == before);
    }
    ++received_fragments;
    free(data);
    return false;
}
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type medium,
                                      uint8_t *data, uint32_t length,
                                      uint32_t fragment_size, cb_send_message *send)
{
    assert(peer == &remote && medium == robusto_mt_halow);
    assert(length == ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1);
    assert(fragment_size + 22 == ROBUSTO_HALOW_TX_PAYLOAD_MAX);
    assert(data != NULL && send != NULL);
    if (check_large_owned) {
        assert(robusto_halow_begin_large_send(peer) == ROB_ERR_QUEUE_FULL);
    }
    if (exercise_small_service) {
        queue_context_t *context = halow_get_queue_context();
        assert(context->count == 3);
        reject_queue_lock = true;
        robusto_halow_service_small_queue();
        reject_queue_lock = false;
        assert(context->count == 3 && small_completed == 0);
        context->blocked = true;
        robusto_halow_service_small_queue();
        assert(context->count == 3 && small_completed == 0);
        context->blocked = false;
        uint8_t incoming[ROBUSTO_HALOW_HEADER_SIZE + 18] = {'R', 'H', 1, 0};
        memcpy(incoming + 8, remote.base_mac_address, 6);
        incoming[14] = 18;
        incoming[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_FRAGMENTED;
        incoming[ROBUSTO_HALOW_HEADER_SIZE + 5] = FRAG_CHECK | 0x80U;
        incoming[ROBUSTO_HALOW_HEADER_SIZE + 6] = 1;
        incoming[ROBUSTO_HALOW_HEADER_SIZE + 14] = 1;
        seal(incoming, sizeof(incoming));
        busy_writes = 1;
        rx_fragment_backpressure = true;
        unsigned long before = now;
        assert(robusto_halow_receive(radio_mac, incoming, sizeof(incoming), NULL) == ROB_OK);
        rx_fragment_backpressure = false;
        assert(now == before && context->count == 3 && small_completed == 0);
        assert(send(peer, data, 10, false) == ROB_OK);
        assert(context->count == 2 && small_completed == 1);
        assert(send(peer, data, 10, false) == ROB_OK);
        assert(context->count == 1 && small_completed == 2);
        uint8_t fragment[10] = {0};
        fragment[4] = MSG_FRAGMENTED;
        fragment[5] = FRAG_MESSAGE;
        uint32_t hash = 1852465904U, index = 42U;
        memcpy(fragment, &hash, sizeof(hash));
        memcpy(fragment + 6, &index, sizeof(index));
        busy_writes = 1;
        before = now;
        assert(send(peer, fragment, sizeof(fragment), false) == ROB_ERR_QUEUE_FULL);
        assert(now == before);
        assert(send(peer, fragment, sizeof(fragment), false) == ROB_OK);
        short_write = 1;
        assert(send(peer, fragment, sizeof(fragment), false) == ROB_ERR_SEND_FAIL);
        short_write = 0;
        assert(send(peer, fragment, 6, false) == ROB_OK);
        assert(robusto_halow_begin_large_send(peer) == ROB_ERR_QUEUE_FULL);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        unsigned before_dumps = trace_dumps;
        robusto_halow_dump_tx_trace();
        assert(trace_dumps == before_dumps + 1);
#endif
    }
    ++fragmented_sends;
    return fragmented_result;
}
void send_work_item(media_queue_item_t *item, robusto_media_t *info, e_media_type medium,
                    send_callback_cb *send, poll_callback_cb *poll, queue_context_t *queue)
{
    (void)info; (void)poll; (void)queue;
    assert(medium == robusto_mt_halow);
    if (item->presentation_mask != 0) {
        /* Dequeued work must retain its reservation while the driver runs. */
        assert(robusto_halow_begin_presentation(item->peer, item->presentation_mask) == ROB_ERR_NOT_READY);
    }
    work_result = send(item->peer, item->data, item->data_length, item->receipt);
    if (item->on_release_cb != NULL) item->on_release_cb(item->peer, item->data_length);
    bool small_owned = item == first_small || item == second_small;
    if (small_owned) {
        assert(held_locks == 0);
        if (item == first_small) first_small = NULL;
        if (item == second_small) second_small = NULL;
        ++small_completed;
    }
    if (free_work_item || small_owned) {
        robusto_free(item->data);
        memset(item, 0xA5, sizeof(*item));
        robusto_free(item);
    }
}

static rob_ret_val_t start(void *context) { (void)context; return start_result; }
static rob_ret_val_t stop(void *context) { (void)context; ++stops; return stop_result; }
static int write_data(void *context, const uint8_t destination[6],
                       const uint8_t *frame, uint32_t length)
{
    (void)context;
    assert(memcmp(destination, radio_mac, 6) == 0);
    assert(length <= ROBUSTO_HALOW_FRAME_MAX);
    if (compare_credit_frame) {
        if (credit_frame_length == 0) {
            memcpy(credit_frame, frame, length);
            credit_frame_length = length;
        } else {
            assert(credit_frame_length == length && memcmp(credit_frame, frame, length) == 0);
        }
    }
    if (always_busy || busy_writes != 0) {
        if (busy_writes != 0) --busy_writes;
        return ROB_ERR_QUEUE_FULL;
    }
    memcpy(written, frame, length);
    written_length = length;
    ++writes;
    if (receipt_on_write && frame[3] == 1) return_receipt = true;
    return short_write ? (int)length - 1 : (int)length;
}

static rob_ret_val_t send_payload(uint32_t length, bool receipt)
{
    uint8_t data[ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1] = {0};
    media_queue_item_t item = {0};
    item.peer = &remote;
    item.data = data;
    item.data_length = ROBUSTO_PREFIX_BYTES + length;
    item.on_release_cb = halow_get_queue_context()->on_release_cb;
    item.receipt = receipt;
    halow_get_queue_context()->on_work_cb(&item);
    return work_result;
}

static void test_backpressure(void)
{
    unsigned long before = now;
    unsigned before_writes = writes;
    check_credit_unlock = true;
    credit_delays = 0;
    compare_credit_frame = true;
    busy_writes = 2;
    assert(send_payload(10, false) == ROB_OK);
    compare_credit_frame = false;
    assert(credit_delays == 2 && now - before == 20 && writes == before_writes + 1);
    busy_writes = 2;
    receipt_on_write = true;
    assert(send_payload(10, true) == ROB_OK);
    receipt_on_write = false;

    always_busy = true;
    before = now;
    before_writes = writes;
    assert(send_payload(10, false) == ROB_ERR_TIMEOUT);
    assert(now - before >= CONFIG_ROB_RECEIPT_TIMEOUT_MS);
    assert(now - before <= CONFIG_ROB_RECEIPT_TIMEOUT_MS + 10U);
    assert(writes == before_writes);
    always_busy = false;
    busy_writes = 1;
    lose_link_on_delay = true;
    assert(send_payload(10, false) == ROB_ERR_NOT_READY);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    check_credit_unlock = false;

    uint8_t frame[ROBUSTO_HALOW_HEADER_SIZE + 6] = {'R', 'H', 1, 1};
    memcpy(frame + 8, remote.base_mac_address, 6);
    frame[14] = 6;
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_MESSAGE;
    seal(frame, sizeof(frame));
    busy_writes = 1;
    before = now;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_QUEUE_FULL);
    assert(now == before);
    uint8_t check[ROBUSTO_HALOW_HEADER_SIZE + 18] = {'R', 'H', 1, 0};
    memcpy(check + 8, remote.base_mac_address, 6);
    check[14] = 18;
    check[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_FRAGMENTED;
    check[ROBUSTO_HALOW_HEADER_SIZE + 5] = FRAG_CHECK | 0x80U;
    check[ROBUSTO_HALOW_HEADER_SIZE + 6] = 1;
    check[ROBUSTO_HALOW_HEADER_SIZE + 14] = 1;
    seal(check, sizeof(check));
    busy_writes = 1;
    rx_fragment_backpressure = true;
    assert(robusto_halow_receive(radio_mac, check, sizeof(check), NULL) == ROB_OK);
    rx_fragment_backpressure = false;
    assert(now == before && busy_writes == 0);
    received_fragments = 0;
}

static void test_large_send_reservation(void)
{
    queue_context_t *queue = halow_get_queue_context();
    assert(queue->on_admit_cb != NULL && queue->on_release_cb != NULL);
    uint32_t boundary = ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX;
    bool was_blocked = queue->blocked;
    queue->blocked = true;
    assert(queue->on_admit_cb(&remote, boundary, true) == ROB_OK);
    queue->on_release_cb(&remote, boundary);
    assert(!remote.halow_large_send_pending);
    assert(queue->on_admit_cb(&remote, boundary + 1, true) == ROB_ERR_QUEUE_FULL);
    assert(!remote.halow_large_send_pending);
    assert(queue->on_admit_cb(&remote, boundary + 1, false) == ROB_OK);
    assert(remote.halow_large_send_pending);
    queue->on_release_cb(&remote, boundary + 1);
    queue->blocked = false;
    queue->count = queue->normal_max_count;
    assert(queue->on_admit_cb(&remote, boundary + 1, true) == ROB_ERR_QUEUE_FULL);
    queue->count = 0;
    assert(queue->on_admit_cb(&remote, boundary + 1, true) == ROB_OK);
    assert(queue->on_admit_cb(&remote, boundary + 1, true) == ROB_ERR_QUEUE_FULL);
    assert(queue->on_admit_cb(&remote, boundary, true) == ROB_OK);
    queue->on_release_cb(&remote, boundary);
    assert(remote.halow_large_send_pending);
    queue->on_release_cb(&remote, boundary + 1);
    assert(!remote.halow_large_send_pending);
    queue->blocked = was_blocked;
    robusto_peer_t other = {0};
    assert(robusto_halow_begin_large_send(NULL) == ROB_ERR_INVALID_ARG);
    reject_queue_lock = true;
    assert(robusto_halow_begin_large_send(&remote) == ROB_ERR_MUTEX);
    reject_queue_lock = false;
    assert(!remote.halow_large_send_pending);
    assert(robusto_halow_begin_large_send(&remote) == ROB_OK);
    assert(robusto_halow_begin_large_send(&remote) == ROB_ERR_QUEUE_FULL);
    assert(send_payload(10, false) == ROB_OK);
    assert(remote.halow_large_send_pending);
    assert(robusto_halow_begin_large_send(&other) == ROB_OK);
    robusto_halow_end_large_send(&other);
    assert(!other.halow_large_send_pending && remote.halow_large_send_pending);
    assert(send_payload(ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1, false) == ROB_OK);
    assert(!remote.halow_large_send_pending);
    assert(robusto_halow_begin_large_send(&remote) == ROB_OK);
    media_queue_item_t *item = robusto_malloc(sizeof(*item));
    assert(item != NULL);
    memset(item, 0, sizeof(*item));
    item->peer = &remote;
    item->data_length = ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1;
    item->on_release_cb = halow_get_queue_context()->on_release_cb;
    item->data = robusto_malloc(item->data_length);
    assert(item->data != NULL);
    free_work_item = check_large_owned = true;
    fragmented_result = ROB_ERR_SEND_FAIL;
    halow_get_queue_context()->on_work_cb(item);
    assert(work_result == ROB_ERR_SEND_FAIL && !remote.halow_large_send_pending);
    free_work_item = check_large_owned = false;
    fragmented_result = ROB_OK;
}

static media_queue_item_t *queued_item(robusto_peer_t *peer, uint32_t payload_length)
{
    media_queue_item_t *item = calloc(1, sizeof(*item));
    assert(item != NULL);
    item->peer = peer;
    item->data_length = ROBUSTO_PREFIX_BYTES + payload_length;
    item->data = calloc(1, item->data_length);
    assert(item->data != NULL);
    halow_get_queue_context()->insert_tail_cb(halow_get_queue_context(), item);
    return item;
}

static void test_small_message_service(void)
{
    robusto_peer_t other = {0};
    assert(robusto_halow_begin_large_send(&remote) == ROB_OK);
    assert(robusto_halow_begin_large_send(&other) == ROB_OK);
    media_queue_item_t *large = queued_item(&other, ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1);
    first_small = queued_item(&remote, 10);
    second_small = queued_item(&remote, 11);
    first_small->receipt = true;
    second_small->receipt = true;
    return_receipt = true;
    exercise_small_service = true;
    assert(send_payload(ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1, false) == ROB_OK);
    exercise_small_service = false;
    return_receipt = false;
    assert(!remote.halow_large_send_pending && other.halow_large_send_pending);
    assert(first_small == NULL && second_small == NULL);
    assert(halow_get_queue_context()->first_queue_item_cb(halow_get_queue_context()) == large);
    halow_get_queue_context()->remove_first_queueitem_cb(halow_get_queue_context());
    robusto_free(large->data);
    robusto_free(large);
    robusto_halow_end_large_send(&other);
    assert(halow_get_queue_context()->count == 0);
    received_fragments = 0;

    uint8_t frame[ROBUSTO_HALOW_FRAME_MAX] = {'R', 'H', 1, 0};
    memcpy(frame + 8, remote.base_mac_address, 6);
    frame[14] = (uint8_t)ROBUSTO_HALOW_PAYLOAD_MAX;
    frame[15] = (uint8_t)(ROBUSTO_HALOW_PAYLOAD_MAX >> 8);
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_MESSAGE;
    seal(frame, sizeof(frame));
    unsigned before = received_messages;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_OK);
    assert(received_messages == before + 1);
    received_messages = before;
}

static void test_presentation_reservation(void)
{
    const uint8_t mask = 1U << presentation_recover;
    queue_context_t *context = halow_get_queue_context();
    assert(robusto_halow_begin_presentation(NULL, mask) == ROB_ERR_INVALID_ARG);
    assert(robusto_halow_begin_presentation(&remote, 0) == ROB_ERR_INVALID_ARG);
    assert(robusto_halow_begin_presentation(&remote, 3) == ROB_ERR_INVALID_ARG);
    assert(robusto_halow_begin_presentation(&remote, 0x80) == ROB_ERR_INVALID_ARG);
    reject_queue_lock = true;
    assert(robusto_halow_begin_presentation(&remote, mask) == ROB_ERR_MUTEX);
    assert(remote.halow_pending_presentations == 0);
    reject_queue_lock = false;
    assert(robusto_halow_begin_presentation(&remote, mask) == ROB_OK);
    for (unsigned i = 0; i < 12; ++i) {
        assert(robusto_halow_begin_presentation(&remote, mask) == ROB_ERR_NOT_READY);
    }
    halow_peer_init_peer(&remote);
    assert(robusto_halow_begin_presentation(&remote, mask) == ROB_ERR_NOT_READY);
    assert(send_payload(10, false) == ROB_ERR_NOT_READY);
    assert(remote.halow_pending_presentations == mask); /* ordinary work cannot clear HI */
    robusto_peer_t other = {0};
    assert(robusto_halow_begin_presentation(&other, mask) == ROB_OK);
    robusto_halow_end_presentation(&other, mask);

    media_queue_item_t *item = calloc(1, sizeof(*item));
    item->peer = &remote;
    item->presentation_mask = mask;
    item->data = calloc(1, ROBUSTO_PREFIX_BYTES + 10);
    item->data_length = ROBUSTO_PREFIX_BYTES + 10;
    assert(robusto_mutex_take(context->__x_queue_mutex, 10000) == ROB_OK);
    context->insert_tail_cb(context, item);
    assert(context->count == 1);
    assert(context->first_queue_item_cb(context) == item);
    context->remove_first_queueitem_cb(context);
    assert(context->count == 0);
    assert(robusto_mutex_give(context->__x_queue_mutex) == ROB_OK);
    assert(robusto_halow_begin_presentation(&remote, mask) == ROB_ERR_NOT_READY);
    free_work_item = true;
    context->on_work_cb(item);
    free_work_item = false;
    assert(work_result == ROB_ERR_NOT_READY);
    assert(remote.halow_pending_presentations == 0);
    assert(robusto_halow_begin_presentation(&remote, mask) == ROB_OK);
    robusto_halow_end_presentation(&remote, mask);
}

int main(void)
{
    assert(init_crc32() == 0);
    memcpy(host.base_mac_address, "\x02\x10\x20\x30\x40\x50", 6);
    memcpy(remote.base_mac_address, "\x02\x11\x22\x33\x44\x55", 6);
    robusto_halow_driver_t binding = {
        .start = start, .stop = stop, .write = write_data,
#ifdef ROBUSTO_HALOW_TEST_WITH_RESET
        .reset_and_reinitialize = reset_driver,
#endif
    };
    assert(robusto_halow_start() == ROB_ERR_NOT_READY);
    assert(robusto_halow_reset_and_reinitialize() == ROB_ERR_NOT_READY);
    assert(robusto_halow_register_driver(NULL) == ROB_ERR_INVALID_ARG);
#ifdef ROBUSTO_HALOW_TEST_SPI_CONFLICT
    assert(robusto_halow_register_driver(&binding) == ROB_OK);
    assert(robusto_halow_init("conflict") == ROB_ERR_INVALID_ARG);
    assert(spi_registrations == 0);
    cleanup_crc32();
    puts("HaLow configured SPI/custom-driver conflict contract passed");
    return 0;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI
    spi_binding = &binding;
    spi_registration_result = ROB_ERR_OUT_OF_MEMORY;
    assert(robusto_halow_init("contract") == ROB_ERR_OUT_OF_MEMORY);
    spi_registration_result = ROB_OK;
#else
    assert(robusto_halow_register_driver(&binding) == ROB_OK);
#endif
    assert(robusto_halow_init("contract") == ROB_OK);
    robusto_halow_rx_stats_t rx_stats = {0};
    assert(robusto_halow_get_rx_stats(&rx_stats) == ROB_ERR_NOT_SUPPORTED);
    assert(rx_stats.spi_frames == 0 && rx_stats.spi_unready == 0 && rx_stats.envelopes == 0);
#ifdef CONFIG_ROBUSTO_HALOW_SPI
    assert(spi_registrations == 2);
#endif
#ifdef ROBUSTO_HALOW_TEST_WITH_RESET
    reset_result = ROB_ERR_TIMEOUT;
    assert(robusto_halow_reset_and_reinitialize() == ROB_ERR_TIMEOUT);
    reset_result = ROB_OK;
    assert(robusto_halow_reset_and_reinitialize() == ROB_OK);
    assert(resets == 2 && stops == 0);
#else
    assert(robusto_halow_reset_and_reinitialize() == ROB_ERR_NOT_SUPPORTED);
#endif
    assert(host.supported_media_types & robusto_mt_halow);
    assert(halow_get_queue_context()->blocked);
    assert(robusto_halow_bind_peer(&remote, radio_mac) == ROB_OK);
    assert(relation_updates == 1);
    test_presentation_reservation();
    assert(robusto_halow_bind_peer(&remote, radio_mac) == ROB_OK);
    assert(relation_updates == 1);
    assert(robusto_halow_bind_peer(&remote, (uint8_t[6]){255,255,255,255,255,255}) == ROB_ERR_INVALID_ARG);
    start_result = ROB_ERR_INIT_FAIL;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    start_result = ROB_OK;
    assert(robusto_halow_start() == ROB_OK);
    assert(send_payload(10, false) == ROB_ERR_NOT_READY);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    assert(!halow_get_queue_context()->blocked);

    assert(send_payload(ROBUSTO_HALOW_TX_PAYLOAD_MAX, false) == ROB_OK);
    assert(written_length == ROBUSTO_HALOW_TX_FRAME_MAX);
    assert(written[0] == 'R' && written[1] == 'H' && written[2] == 1 && written[3] == 0);
    assert(send_payload(ROBUSTO_HALOW_TX_PAYLOAD_MAX + 1, false) == ROB_OK);
    assert(fragmented_sends == 1);
    test_backpressure();
    test_large_send_reservation();
    test_small_message_service();
    short_write = 1;
    assert(send_payload(10, false) == ROB_ERR_SEND_FAIL);
    short_write = 0;

    return_receipt = true;
    assert(send_payload(10, true) == ROB_OK);
    for (bad_receipt = 1; bad_receipt <= 3; ++bad_receipt) {
        return_receipt = true;
        assert(send_payload(10, true) == ROB_ERR_NO_RECEIPT);
    }
    bad_receipt = 0;
    assert(send_payload(10, true) == ROB_ERR_NO_RECEIPT);

    uint8_t frame[ROBUSTO_HALOW_HEADER_SIZE + 6] = { 'R', 'H', 1, 1 };
    memcpy(frame + 8, remote.base_mac_address, 6);
    frame[14] = 6;
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_MESSAGE;
    seal(frame, sizeof(frame));
    int16_t rssi = -67;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), &rssi) == ROB_OK);
    assert(received_messages == 1);
    assert(remote.halow_info.latest_rssi_valid && remote.halow_info.latest_rssi_dbm == rssi);
    assert(written_length == ROBUSTO_HALOW_HEADER_SIZE && written[3] == 2);
    memcpy(frame + 8, "\x02\x99\x88\x77\x66\x55", 6);
    seal(frame, sizeof(frame));
    unsigned old_writes = writes;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_PEER_NOT_FOUND);
    assert(writes == old_writes);
    memset(frame + 8, 0, 6);
    seal(frame, sizeof(frame));
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_PARSING_FAILED);
    assert(writes == old_writes);
    memcpy(frame + 8, remote.base_mac_address, 6);
    seal(frame, sizeof(frame));
    assert(robusto_halow_receive((uint8_t[6]){0}, frame, sizeof(frame), NULL) == ROB_ERR_INVALID_ARG);
    assert(writes == old_writes);
    frame[ROBUSTO_HALOW_HEADER_SIZE] ^= 1;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_WRONG_CRC);
    assert(writes == old_writes);
    assert(robusto_halow_receive(radio_mac, frame, 3, NULL) == ROB_ERR_INVALID_ARG);
    seal(frame, sizeof(frame));
    frame[14] = 7;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_PARSING_FAILED);
    frame[14] = 6;
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_FRAGMENTED;
    frame[14] = 5;
    seal(frame, sizeof(frame) - 1);
    old_writes = writes;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame) - 1, NULL) == ROB_ERR_MESSAGE_TOO_SHORT);
    assert(writes == old_writes);
    frame[14] = 6;
    frame[ROBUSTO_HALOW_HEADER_SIZE + 5] = FRAG_CHECK;
    seal(frame, sizeof(frame));
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_PARSING_FAILED);
    uint8_t check_v2[ROBUSTO_HALOW_HEADER_SIZE + 18] = {'R', 'H', 1, 0};
    memcpy(check_v2 + 8, remote.base_mac_address, 6);
    check_v2[14] = 18;
    check_v2[ROBUSTO_HALOW_HEADER_SIZE + 4] = MSG_FRAGMENTED;
    check_v2[ROBUSTO_HALOW_HEADER_SIZE + 5] = FRAG_CHECK | 0x80U;
    seal(check_v2, sizeof(check_v2));
    assert(robusto_halow_receive(radio_mac, check_v2, sizeof(check_v2), NULL) == ROB_ERR_PARSING_FAILED);
    check_v2[ROBUSTO_HALOW_HEADER_SIZE + 6] = 1;
    seal(check_v2, sizeof(check_v2));
    assert(robusto_halow_receive(radio_mac, check_v2, sizeof(check_v2), NULL) == ROB_ERR_PARSING_FAILED);
    check_v2[ROBUSTO_HALOW_HEADER_SIZE + 14] = 1;
    seal(check_v2, sizeof(check_v2));
    assert(robusto_halow_receive(radio_mac, check_v2, sizeof(check_v2), NULL) == ROB_OK);
    assert(received_fragments == 1);
    const uint8_t subtypes[] = { FRAG_REQUEST, FRAG_MESSAGE, FRAG_RESULT };
    for (unsigned i = 0; i < sizeof(subtypes); ++i) {
        frame[ROBUSTO_HALOW_HEADER_SIZE + 5] = subtypes[i] | 0x80U;
        seal(frame, sizeof(frame));
        old_writes = writes;
        assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_MESSAGE_TOO_SHORT);
        assert(writes == old_writes);
    }
    frame[ROBUSTO_HALOW_HEADER_SIZE + 5] = 0xff;
    seal(frame, sizeof(frame));
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_PARSING_FAILED);
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = HEARTBEAT_CONTEXT;
    seal(frame, sizeof(frame));
    old_writes = writes;
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_MESSAGE_TOO_SHORT);
    assert(writes == old_writes && received_heartbeats == 0);
    uint8_t heartbeat[ROBUSTO_HALOW_HEADER_SIZE + 7] = { 'R', 'H', 1, 1 };
    memcpy(heartbeat + 8, remote.base_mac_address, 6);
    heartbeat[14] = 7;
    heartbeat[ROBUSTO_HALOW_HEADER_SIZE + 4] = HEARTBEAT_CONTEXT;
    seal(heartbeat, sizeof(heartbeat));
    assert(robusto_halow_receive(radio_mac, heartbeat, sizeof(heartbeat), NULL) == ROB_OK);
    assert(received_heartbeats == 1 && remote.halow_info.last_peer_receive == 123);
    memcpy(frame + 8, "\x02\x99\x88\x77\x66\x55", 6);
    frame[ROBUSTO_HALOW_HEADER_SIZE + 4] = 0x42;
    seal(frame, sizeof(frame));
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_OK);
    assert(peer_creations == 1 && remote.halow_address_valid);
    assert(memcmp(remote.halow_mac_address, radio_mac, 6) == 0);
    assert(robusto_halow_set_link_ready(false) == ROB_OK);
    assert(halow_get_queue_context()->blocked);
    assert(send_payload(10, false) == ROB_ERR_NOT_READY);
    stop_result = ROB_ERR_SEND_FAIL;
    assert(robusto_halow_stop() == ROB_ERR_SEND_FAIL);
    assert(stops == 1);
    stop_result = ROB_OK;
    assert(robusto_halow_stop() == ROB_OK);
    assert(stops == 2);
    assert(robusto_halow_stop() == ROB_ERR_NOT_READY);
    assert(stops == 2);
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_ERR_NOT_READY);
    assert(robusto_halow_set_link_ready(true) == ROB_ERR_NOT_READY);
    assert(robusto_halow_start() == ROB_OK);
    assert(halow_get_queue_context()->blocked);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    assert(send_payload(10, false) == ROB_OK);
    assert(robusto_halow_stop() == ROB_OK);
    assert(logs > 0);
    cleanup_crc32();
    puts("HaLow adapter contracts passed");
    return 0;
}
