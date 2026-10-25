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
unsigned long r_millis(void) { return now; }
unsigned long r_micros(void) { return now * 1000; }
void r_delay(unsigned long delay)
{
    now += delay;
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
    (void)level; (void)tag; (void)format;
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
    return ROB_OK;
}
rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex)
{
    assert(mutex != NULL);
    assert(*(uint8_t *)mutex == 1);
    *(uint8_t *)mutex = 0;
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
    assert(fragment_size == ROBUSTO_HALOW_PAYLOAD_MAX - 10);
    assert(send != NULL);
    ++received_fragments;
    free(data);
    return false;
}
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, e_media_type medium,
                                      uint8_t *data, uint32_t length,
                                      uint32_t fragment_size, cb_send_message *send)
{
    assert(peer == &remote && medium == robusto_mt_halow);
    assert(length == ROBUSTO_HALOW_PAYLOAD_MAX + 1);
    assert(fragment_size + 10 == ROBUSTO_HALOW_PAYLOAD_MAX);
    assert(data != NULL && send != NULL);
    ++fragmented_sends;
    return ROB_OK;
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
    if (free_work_item) {
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
    memcpy(written, frame, length);
    written_length = length;
    ++writes;
    return short_write ? (int)length - 1 : (int)length;
}

static rob_ret_val_t send_payload(uint32_t length, bool receipt)
{
    uint8_t data[ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_PAYLOAD_MAX + 1] = {0};
    media_queue_item_t item = {0};
    item.peer = &remote;
    item.data = data;
    item.data_length = ROBUSTO_PREFIX_BYTES + length;
    item.receipt = receipt;
    halow_get_queue_context()->on_work_cb(&item);
    return work_result;
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

    assert(send_payload(ROBUSTO_HALOW_PAYLOAD_MAX, false) == ROB_OK);
    assert(written_length == 1642);
    assert(written[0] == 'R' && written[1] == 'H' && written[2] == 1 && written[3] == 0);
    assert(send_payload(ROBUSTO_HALOW_PAYLOAD_MAX + 1, false) == ROB_OK);
    assert(fragmented_sends == 1);
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
    assert(robusto_halow_receive(radio_mac, frame, sizeof(frame), NULL) == ROB_OK);
    assert(received_fragments == 1);
    const uint8_t subtypes[] = { FRAG_REQUEST, FRAG_MESSAGE, FRAG_RESULT };
    for (unsigned i = 0; i < sizeof(subtypes); ++i) {
        frame[ROBUSTO_HALOW_HEADER_SIZE + 5] = subtypes[i];
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
