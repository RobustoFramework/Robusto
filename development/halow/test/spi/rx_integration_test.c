#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <robusto_halow.h>
#include <robusto_incoming.h>
#include <robusto_message.h>
#include <robusto_network_init.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_queue.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include "compat_crc32.h"
#include "halow_spi_wire.h"
#include "hgic_raw.h"

#define MESSAGE_SIZE 55002U
struct hgic_raw hgic;
static robusto_peer_t host, remote;
static const uint8_t radio_mac[6] = {2, 1, 2, 3, 4, 5};
static uint8_t expected[MESSAGE_SIZE];
static uint8_t wire[ROBUSTO_HALOW_SPI_RX_CAPACITY];
static uint8_t payload[ROBUSTO_HALOW_PAYLOAD_MAX];
static uint32_t chunk, fragments, hash, sequence;
static uint16_t cookie;
static unsigned deliveries, allocations, receipts, resends, results;
static unsigned persistent_allocations;
static unsigned request_logs, progress_logs, completion_logs, handoff_errors;
static unsigned envelope_logs, wire_receives;
static unsigned trace_entries, trace_fragments, trace_entry_logs, trace_fragment_logs;
static unsigned snapshot_logs;
static char last_snapshot[768];
static rob_ret_val_t snapshot_result = ROB_OK;
static int16_t last_result;
static rob_ret_val_t incoming_result = ROB_OK;
static unsigned long now;

static uint32_t read32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint32_t robusto_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    return crc32_be_compat(crc, data, length);
}
unsigned char crc7_be(unsigned char crc, const unsigned char *data, unsigned int length)
{
    (void)crc; (void)data; (void)length;
    assert(!"RX integration must not issue CMD52 diagnostics");
    return 0;
}
void *robusto_malloc(size_t length)
{
    void *data = malloc(length);
    assert(data != NULL);
    ++allocations;
    return data;
}
void robusto_free(void *data)
{
    if (data != NULL) {
        assert(allocations > 0);
        --allocations;
        free(data);
    }
}
unsigned long r_millis(void) { return now; }
unsigned long r_micros(void) { return now * 1000; }
void r_delay(unsigned long delay) { now += delay; }
void robusto_yield(void) { assert(!"RX integration must not wait for outgoing receipts"); }
void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...)
{
    (void)tag;
    if (strstr(format, "RXTRACE HaLow") != NULL) {
        assert(level == ROB_LOG_INFO && strstr(format, "unvalidated=1") != NULL);
        ++trace_entry_logs;
    }
    if (strstr(format, "RXTRACE FRAG_MESSAGE") != NULL) {
        assert(level == ROB_LOG_INFO && strstr(format, "unvalidated=1") != NULL);
        ++trace_fragment_logs;
    }
    if (strstr(format, "HaLow RX all-missing") != NULL) {
        assert(level == ROB_LOG_WARN);
        va_list arguments;
        va_start(arguments, format);
        int length = vsnprintf(last_snapshot, sizeof(last_snapshot), format, arguments);
        va_end(arguments);
        assert(length > 0 && (size_t)length < sizeof(last_snapshot));
        ++snapshot_logs;
    }
    if (strstr(format, "RX envelope validated") != NULL) {
        assert(level == ROB_LOG_INFO);
        ++envelope_logs;
    }
    if (strstr(format, "HaLow RX transfer request") != NULL) {
        assert(level == ROB_LOG_INFO);
        ++request_logs;
    }
    if (strstr(format, "HaLow RX fragment stored") != NULL) {
        assert(level == ROB_LOG_INFO);
        ++progress_logs;
    }
    if (strstr(format, "HaLow RX reassembled") != NULL) {
        assert(level == ROB_LOG_INFO);
        ++completion_logs;
    }
    if (strstr(format, "HaLow RX incoming handoff failed") != NULL) {
        assert(level == ROB_LOG_ERROR);
        ++handoff_errors;
    }
}
void rob_log_bit_mesh(rob_log_level_t level, const char *tag, const uint8_t *data, int length)
{
    (void)level; (void)tag; (void)data; (void)length;
}
mutex_ref_t robusto_mutex_init(void) { return calloc(1, 1); }
void robusto_mutex_deinit(mutex_ref_t mutex) { free(mutex); }
rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout)
{
    (void)timeout;
    assert(mutex != NULL && *(uint8_t *)mutex == 0);
    *(uint8_t *)mutex = 1;
    return ROB_OK;
}
rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex)
{
    assert(mutex != NULL && *(uint8_t *)mutex == 1);
    *(uint8_t *)mutex = 0;
    return ROB_OK;
}
rob_ret_val_t init_work_queue(queue_context_t *queue, const char *prefix, const char *name)
{
    (void)prefix; (void)name;
    queue->__x_queue_mutex = robusto_mutex_init();
    assert(queue->__x_queue_mutex != NULL);
    return ROB_OK;
}
void set_queue_blocked(queue_context_t *queue, bool blocked) { queue->blocked = blocked; }
void send_work_item(media_queue_item_t *item, robusto_media_t *info, e_media_type medium,
                    send_callback_cb *send, poll_callback_cb *poll, queue_context_t *queue)
{
    (void)item; (void)info; (void)medium; (void)send; (void)poll; (void)queue;
    assert(!"RX integration must not run the outgoing queue");
}
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
robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type medium)
{
    assert(peer == &remote && medium == robusto_mt_halow);
    return &peer->halow_info;
}
robusto_peer_t *get_host_peer(void) { return &host; }
void add_host_supported_media_type(e_media_type medium) { host.supported_media_types |= medium; }
robusto_peer_t *robusto_peers_find_peer_by_base_mac_address(rob_mac_address *mac)
{
    return memcmp(*mac, remote.base_mac_address, 6) == 0 ? &remote : NULL;
}
void robusto_peer_update_relation(robusto_peer_t *peer) { assert(peer == &remote); }
robusto_peer_t *robusto_add_init_new_peer(const char *name, rob_mac_address *mac,
                                         robusto_media_types media)
{
    (void)name; (void)mac; (void)media;
    assert(!"Unknown fragment sender must not create a peer");
    return NULL;
}
uint64_t parse_heartbeat(const uint8_t *data, uint8_t prefix)
{
    (void)data; (void)prefix;
    assert(!"Fragment input must not dispatch as a heartbeat");
    return 0;
}
rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t length,
                                     robusto_peer_t *peer, e_media_type medium, int offset)
{
    assert(peer == &remote && medium == robusto_mt_halow && offset == 0);
    assert(length == MESSAGE_SIZE && memcmp(data, expected, length) == 0);
    ++deliveries;
    robusto_free(data);
    return incoming_result;
}
static rob_ret_val_t start(void *context) { (void)context; return ROB_OK; }
static rob_ret_val_t stop(void *context) { (void)context; return ROB_OK; }
static rob_ret_val_t get_rx_stats(void *context, robusto_halow_rx_stats_t *out)
{
    (void)context;
    if (snapshot_result == ROB_OK) {
        out->spi_frames = wire_receives;
        out->spi_unready = 0;
    }
    return snapshot_result;
}
static int write_frame(void *context, const uint8_t destination[6],
                        const uint8_t *frame, uint32_t length)
{
    (void)context;
    assert(memcmp(destination, radio_mac, 6) == 0);
    assert(length >= ROBUSTO_HALOW_HEADER_SIZE && length <= ROBUSTO_HALOW_TX_FRAME_MAX);
    uint32_t crc = robusto_crc32(0, frame, 16);
    crc = robusto_crc32(crc, frame + ROBUSTO_HALOW_HEADER_SIZE,
                        length - ROBUSTO_HALOW_HEADER_SIZE);
    assert(crc == read32(frame + 16));
    if (frame[3] == 2) {
        assert(length == ROBUSTO_HALOW_HEADER_SIZE);
        ++receipts;
    } else {
        const uint8_t *body = frame + ROBUSTO_HALOW_HEADER_SIZE;
        assert(body[4] == MSG_FRAGMENTED);
        assert(read32(body) == hash);
        assert(body[5] & 0x80U);
        if ((body[5] & 0x7fU) == FRAG_RESEND) {
            assert(length == ROBUSTO_HALOW_HEADER_SIZE + 18 + fragments);
            assert(body[19] == 0);
            ++resends;
        } else {
            assert(body[5] == (FRAG_RESULT | 0x80U) && length == ROBUSTO_HALOW_HEADER_SIZE + 20);
            last_result = (int16_t)get_unaligned_le16(body + 18);
            ++results;
        }
    }
    return (int)length;
}
static uint32_t transfer_id, recovery_round;
static size_t wrap_payload(uint32_t length, bool receipt)
{
    uint32_t legacy_length = length;
    length += 12;
    const size_t raw_length = ROBUSTO_HALOW_HEADER_SIZE + length;
    const size_t wire_length = 22 + raw_length;
    assert(wire_length <= 1664);
    memset(wire, 0, sizeof(wire));
    put_unaligned_le16(HGIC_HDR_RX_MAGIC, wire);
    wire[2] = HGIC_HDR_TYPE_FRM2;
    put_unaligned_le16((uint16_t)wire_length, wire + 4);
    put_unaligned_le16(cookie++, wire + 6);
    memcpy(wire + 14, radio_mac, 6);
    wire[20] = 0x66;
    wire[21] = 0xaa;
    uint8_t *frame = wire + 22;
    frame[0] = 'R'; frame[1] = 'H'; frame[2] = 1; frame[3] = receipt ? 1 : 0;
    put_unaligned_le32(++sequence, frame + 4);
    memcpy(frame + 8, remote.base_mac_address, 6);
    put_unaligned_le16((uint16_t)length, frame + 14);
    uint8_t *body = frame + ROBUSTO_HALOW_HEADER_SIZE;
    memcpy(body, payload, 6);
    body[5] |= 0x80U;
    put_unaligned_le32(transfer_id, body + 6);
    put_unaligned_le32(0, body + 10);
    put_unaligned_le32(payload[5] == FRAG_REQUEST ? 0 : recovery_round, body + 14);
    memcpy(body + 18, payload + 6, legacy_length - 6);
    uint32_t crc = robusto_crc32(0, frame, 16);
    crc = robusto_crc32(crc, frame + ROBUSTO_HALOW_HEADER_SIZE, length);
    put_unaligned_le32(crc, frame + 16);
    return wire_length;
}
static rob_ret_val_t receive_wire(size_t length)
{
    robusto_halow_spi_packet_t packet;
    assert(robusto_halow_spi_parse(wire, length, &packet));
    assert(packet.frame == wire + 22 && packet.length == get_unaligned_le16(wire + 4) - 22U);
    ++wire_receives;
    ++trace_entries;
    if (packet.length >= ROBUSTO_HALOW_HEADER_SIZE + 6U &&
        packet.frame[3] <= 1 &&
        (packet.frame[ROBUSTO_HALOW_HEADER_SIZE + 4U] & 7U) == MSG_FRAGMENTED &&
        (packet.frame[ROBUSTO_HALOW_HEADER_SIZE + 5U] & 0x7fU) == FRAG_MESSAGE) {
        ++trace_fragments;
    }
    return robusto_halow_receive(packet.source, packet.frame, (uint32_t)packet.length, NULL);
}
static void request(void)
{
    memset(payload, 0, 22);
    payload[4] = MSG_FRAGMENTED;
    payload[5] = FRAG_REQUEST;
    put_unaligned_le32(MESSAGE_SIZE, payload + 6);
    put_unaligned_le32(fragments, payload + 10);
    put_unaligned_le32(chunk, payload + 14);
    put_unaligned_le32(hash, payload + 18);
    put_unaligned_le32(robusto_crc32(0, payload + 4, 18), payload);
    assert(receive_wire(wrap_payload(22, true)) == ROB_OK);
}
static void fragment(uint32_t index, bool corrupt)
{
    uint32_t offset = index * chunk;
    uint32_t length = MESSAGE_SIZE - offset;
    if (length > chunk) length = chunk;
    put_unaligned_le32(hash, payload);
    payload[4] = MSG_FRAGMENTED;
    payload[5] = FRAG_MESSAGE;
    put_unaligned_le32(index, payload + 6);
    memcpy(payload + 10, expected + offset, length);
    if (corrupt) payload[10] ^= 1;
    size_t wire_length = wrap_payload(10 + length, false);
    assert(receive_wire((wire_length + 511U) & ~511U) == ROB_OK);
}
static void check(void)
{
    ++recovery_round;
    put_unaligned_le32(hash, payload);
    payload[4] = MSG_FRAGMENTED;
    payload[5] = FRAG_CHECK;
    assert(receive_wire(wrap_payload(6, false)) == ROB_OK);
}
static void reset_transfer(uint32_t size)
{
    assert(allocations == persistent_allocations);
    robusto_message_fragment_init("rx-integration");
    robusto_halow_spi_reset_rx_cookie();
    chunk = size;
    ++transfer_id;
    recovery_round = 0;
    fragments = (MESSAGE_SIZE + chunk - 1U) / chunk;
    hash = robusto_crc32(0, expected, sizeof(expected));
    receipts = resends = results = 0;
    request_logs = progress_logs = completion_logs = handoff_errors = 0;
    incoming_result = ROB_OK;
    snapshot_logs = 0;
    snapshot_result = ROB_OK;
}

static long long snapshot_value(const char *name)
{
    char field[48];
    int length = snprintf(field, sizeof(field), "%s=", name);
    assert(length > 0 && (size_t)length < sizeof(field));
    const char *value = strstr(last_snapshot, field);
    assert(value != NULL);
    char *end;
    long long result = strtoll(value + length, &end, 10);
    assert(end != value + length &&
           (*end == ' ' || *end == '\0' || *end == '\n' || *end == '\033'));
    return result;
}

static void test_all_missing_snapshot(void)
{
    reset_transfer(994);
    request();
    check();
    assert(snapshot_logs == 1);
    assert(snapshot_value("hash") == hash);
    assert(snapshot_value("stats_level") == ROBUSTO_STATS_LEVEL_VERBOSE);
    assert(snapshot_value("rx_req") == 1 && snapshot_value("rx_msg") == 0);
    assert(snapshot_value("spi_frames") == wire_receives);
    assert(snapshot_value("envelopes") == wire_receives && snapshot_value("spi_rc") == ROB_OK);
    assert(snapshot_value("invalid_ref") == 0 && snapshot_value("invalid_index") == 0);
    assert(snapshot_value("wrong_len") == 0 && snapshot_value("crc") == 0);
    assert(snapshot_value("oom") == 0 && snapshot_value("spi_unready") == 0);

    payload[4] = MSG_FRAGMENTED; payload[5] = FRAG_MESSAGE;
    put_unaligned_le32(hash ^ 1U, payload);
    put_unaligned_le32(0, payload + 6);
    assert(receive_wire(wrap_payload(10, false)) == ROB_OK);
    put_unaligned_le32(hash, payload);
    put_unaligned_le32(fragments, payload + 6);
    assert(receive_wire(wrap_payload(10, false)) == ROB_OK);
    put_unaligned_le32(0, payload + 6);
    assert(receive_wire(wrap_payload(10, false)) == ROB_OK);
    memset(payload, 0, 22);
    payload[4] = MSG_FRAGMENTED; payload[5] = FRAG_REQUEST;
    assert(robusto_crc32(0, payload + 4, 18) != 0);
    assert(receive_wire(wrap_payload(22, true)) == ROB_OK);
    check();
    assert(snapshot_logs == 2);
    assert(snapshot_value("rx_req") == 1 && snapshot_value("rx_msg") == 3);
    assert(snapshot_value("invalid_ref") == 1 && snapshot_value("invalid_index") == 1);
    assert(snapshot_value("wrong_len") == 1 && snapshot_value("crc") == 1);
    assert(snapshot_value("spi_frames") == wire_receives);
    assert(snapshot_value("envelopes") == wire_receives);

    snapshot_result = ROB_ERR_NOT_SUPPORTED;
    check();
    assert(snapshot_value("spi_rc") == ROB_ERR_NOT_SUPPORTED);
    assert(snapshot_value("envelopes") == wire_receives);
    snapshot_result = ROB_ERR_MUTEX;
    check();
    assert(snapshot_value("spi_rc") == ROB_ERR_MUTEX);
    assert(snapshot_value("envelopes") == wire_receives);
    assert(snapshot_logs == 4);
    snapshot_result = ROB_OK;
    fragment(0, false);
    check();
    assert(snapshot_logs == 4);
    for (uint32_t i = 1; i < fragments; ++i) fragment(i, false);
    assert(last_result == ROB_OK && allocations == persistent_allocations);
    puts("All-missing snapshots distinguish no fragment entries from rejected fragments, and expose unavailable/failed SPI snapshots without changing recovery.");
}

static void test_early_receive_trace(void)
{
    reset_transfer(994);
    put_unaligned_le32(hash, payload);
    payload[4] = MSG_FRAGMENTED;
    payload[5] = FRAG_MESSAGE;
    put_unaligned_le32(0, payload + 6);
    wrap_payload(10, false);
    wire[22 + 16] ^= 1;
    assert(robusto_halow_receive(radio_mac, wire + 22, 42, NULL) == ROB_ERR_WRONG_CRC);
    assert(trace_entry_logs == 1 && trace_fragment_logs == 1);
    assert(robusto_halow_receive(NULL, NULL, 0, NULL) == ROB_ERR_INVALID_ARG);
    assert(trace_entry_logs == 2 && trace_fragment_logs == 1);
    assert(robusto_halow_set_link_ready(false) == ROB_OK);
    assert(robusto_halow_receive(radio_mac, wire + 22, 42, NULL) == ROB_ERR_NOT_READY);
    assert(trace_entry_logs == 3 && trace_fragment_logs == 2);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    robusto_fragment_stats_t stats;
    robusto_fragment_stats_get(&stats, NULL);
    assert(stats.message_received == 0 && allocations == persistent_allocations);
    trace_entries = 3;
    trace_fragments = 2;
    puts("Temporary HaLow entry trace sees fragment candidates before CRC/readiness rejection and safely handles null/short frames.");
}

int main(void)
{
    assert(init_crc32() == 0);
    persistent_allocations = allocations;
    memcpy(host.base_mac_address, "\x02\xaa\xbb\xcc\xdd\xee", 6);
    memcpy(remote.base_mac_address, "\x02\x11\x22\x33\x44\x55", 6);
    const robusto_halow_driver_t driver = {
        .start = start, .stop = stop, .write = write_frame, .get_rx_stats = get_rx_stats
    };
    assert(robusto_halow_register_driver(&driver) == ROB_OK);
    assert(robusto_halow_init("rx-integration") == ROB_OK);
    assert(robusto_halow_start() == ROB_OK);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    for (unsigned i = 0; i < MESSAGE_SIZE; ++i) expected[i] = (uint8_t)(i * 17U);
    test_early_receive_trace();
    const uint32_t chunks[] = {214, 982, 1600};
    for (unsigned trial = 0; trial < 3; ++trial) {
        reset_transfer(chunks[trial]);
        unsigned before = deliveries;
        request();
        request();
        assert(request_logs == 1 && receipts == 2);
        for (uint32_t i = 0; i < fragments; ++i) {
            if (i != 1) fragment(i, false);
            if (i == 0) fragment(i, false);
        }
        assert(deliveries == before);
        check();
        assert(resends == 1);
        fragment(1, false);
        assert(deliveries == before + 1 && allocations == persistent_allocations);
        assert(results == 1 && last_result == ROB_OK && completion_logs == 1);
        unsigned expected_progress = 1 + fragments / 32U + (fragments % 32U != 0);
        assert(progress_logs == expected_progress);
        check();
        assert(results == 2 && deliveries == before + 1);
        printf("Integrated SPI/HaLow RX: 55KB chunk=%u, padded reads, duplicates, missing-fragment recovery and bounded INFO logs passed\n", chunk);
    }
    assert(envelope_logs == 1 + wire_receives / 64U);
    test_all_missing_snapshot();
    reset_transfer(994);
    unsigned before = deliveries;
    request();
    for (uint32_t i = 0; i < fragments; ++i) fragment(i, i == 1);
    robusto_fragment_stats_t stats;
    robusto_fragment_stats_get(&stats, NULL);
    assert(deliveries == before && last_result == ROB_ERR_WRONG_CRC);
    assert(stats.full_message_crc_mismatch == 1 && completion_logs == 0 &&
           allocations == persistent_allocations);

    reset_transfer(994);
    request();
    incoming_result = ROB_ERR_QUEUE_FULL;
    for (uint32_t i = 0; i < fragments; ++i) fragment(i, false);
    assert(handoff_errors == 1 && completion_logs == 0 && allocations == persistent_allocations);
    assert(last_result == ROB_ERR_QUEUE_FULL);
    before = deliveries;
    check();
    request();
    fragment(0, false);
    assert(last_result == ROB_ERR_QUEUE_FULL && deliveries == before);

    reset_transfer(994);
    put_unaligned_le32(hash, payload);
    payload[4] = MSG_FRAGMENTED; payload[5] = FRAG_MESSAGE;
    put_unaligned_le32(0, payload + 6);
    size_t length = wrap_payload(10, false);
    robusto_halow_spi_packet_t packet;
    assert(!robusto_halow_spi_parse(wire, length - 1, &packet));
    wire[20] = 0;
    assert(!robusto_halow_spi_parse(wire, length, &packet));
    wire[20] = 0x66;
    wire[22 + ROBUSTO_HALOW_HEADER_SIZE] ^= 1;
    assert(receive_wire(length) == ROB_ERR_WRONG_CRC);
    length = wrap_payload(9, false);
    assert(receive_wire(length) == ROB_ERR_MESSAGE_TOO_SHORT);
    length = wrap_payload(10, false);
    assert(robusto_halow_set_link_ready(false) == ROB_OK);
    assert(receive_wire(length) == ROB_ERR_NOT_READY);
    assert(robusto_halow_set_link_ready(true) == ROB_OK);
    assert(receive_wire(length) == ROB_OK);
    robusto_fragment_stats_get(&stats, NULL);
    assert(stats.message_received == 1 && stats.invalid_fragment_reference == 1);
    assert(allocations == persistent_allocations && request_logs == 0);
    assert(robusto_halow_stop() == ROB_OK);
    unsigned expected_entries = trace_entries <= 128U
        ? trace_entries : 128U + trace_entries / 64U - 2U;
    unsigned expected_fragments = trace_fragments <= 32U
        ? trace_fragments : 32U + trace_fragments / 64U;
    assert(trace_entry_logs == expected_entries && trace_fragment_logs == expected_fragments);
    cleanup_crc32();
    puts("Integrated RX rejects malformed wire/envelopes, CRC errors and unready links; CRC failure and incoming handoff failure remain explicit.");
    return 0;
}
