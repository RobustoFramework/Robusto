#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <robusto_halow.h>
#include <robusto_concurrency.h>
#include <robusto_incoming.h>
#include <robusto_message.h>
#include <robusto_network_init.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include "compat_crc32.h"

#define MAX_MESSAGE_SIZE 55002U
static uint32_t chunk_size = ROBUSTO_HALOW_TX_PAYLOAD_MAX - 22U;
static uint32_t message_size = MAX_MESSAGE_SIZE;
#define CHUNK chunk_size
#define MESSAGE_SIZE message_size
#define FRAGMENT_COUNT ((MESSAGE_SIZE + CHUNK - 1U) / CHUNK)

typedef struct packet {
    struct packet *next;
    robusto_peer_t *receiver_view;
    uint8_t *data;
    uint32_t length;
} packet_t;

static robusto_peer_t sender_view;
static robusto_peer_t receiver_view;
static packet_t *head;
static packet_t **tail = &head;
static uint8_t expected[MAX_MESSAGE_SIZE];
static e_media_type active_medium = robusto_mt_halow;
static robusto_media_t sender_media, receiver_media;
static unsigned deliveries;
static unsigned receipt_requests;
static unsigned data_fragments;
static bool drop_fragment;
static unsigned long now;
static bool apply_backpressure;
static unsigned burst_frames, busy_returns;
static bool hard_failure;
static unsigned fragment_attempts;
static rob_ret_val_t one_shot_error = ROB_OK;
static bool local_error_injected, yield_recorded;
static unsigned initial_burst_attempts;
static unsigned small_service_calls;
static bool in_rx;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
static unsigned trace_dumps;
static bool trace_fail_request;
void robusto_halow_dump_tx_trace(void)
{
    assert(!in_rx);
    ++trace_dumps;
}
#endif
static unsigned busy_until;
static bool permanent_busy, busy_index_pending;
static uint32_t busy_index;
static unsigned busy_checks;
static unsigned service_delay;
static unsigned feedback_delay;
static int fail_after = -1;
static bool control_busy;
static uint8_t control_type, control_packet[34];
static unsigned long control_resume_at;
static unsigned control_length, control_busy_checks;
static unsigned recovery_scenario, recovery_attempts[512], feedback_packets, result_packets;
static uint8_t saved_request[34], saved_check[18], saved_data[1664], saved_result[20];
static uint32_t saved_data_length;
static uint8_t previous_map[530];
static uint32_t previous_map_length;
static rob_ret_val_t incoming_status = ROB_OK;
static unsigned message_delay, unmatched_feedback_warnings;
static bool track_completion_replies;
static unsigned completion_replies;

static void enqueue_packet(robusto_peer_t *peer, const uint8_t *data, uint32_t length)
{
    packet_t *packet = malloc(sizeof(*packet));
    assert(packet != NULL);
    packet->next = NULL;
    packet->receiver_view = peer == &sender_view ? &receiver_view : &sender_view;
    packet->data = malloc(length);
    assert(packet->data != NULL);
    memcpy(packet->data, data, length);
    packet->length = length;
    *tail = packet;
    tail = &packet->next;
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

void robusto_halow_service_small_queue(void)
{
    assert(active_medium == robusto_mt_halow);
    now += service_delay;
    service_delay = 0;
    ++small_service_calls;
}

rob_ret_val_t robusto_halow_get_rx_stats(robusto_halow_rx_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    return ROB_ERR_NOT_SUPPORTED;
}

uint32_t robusto_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    return crc32_be_compat(crc, data, length);
}
void *robusto_malloc(size_t length)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    return malloc(length);
}
void robusto_free(void *data) { free(data); }
unsigned long r_millis(void) { return now; }
unsigned long r_micros(void) { return now * 1000; }
void r_delay(unsigned long delay) { now += delay; }
void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...)
{
    (void)tag;
    (void)level;
    assert(strstr(format, "HaLow fragment TX attempt") == NULL);
    if (strstr(format, "Ignoring late/unmatched HaLow fragment feedback") != NULL)
        ++unmatched_feedback_warnings;
}
void rob_log_bit_mesh(rob_log_level_t level, const char *prefix, const uint8_t *data, int length)
{
    (void)level; (void)prefix; (void)data; (void)length;
}
robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type medium)
{
    assert(medium == active_medium);
    assert(peer == &sender_view || peer == &receiver_view);
    return peer == &sender_view ? &sender_media : &receiver_media;
}
void add_to_history(robusto_media_t *info, bool sending, rob_ret_val_t result)
{
    (void)info; (void)sending; (void)result;
}

static rob_ret_val_t send_packet(robusto_peer_t *peer, uint8_t *data, uint32_t length, bool receipt)
{
    uint32_t extension = active_medium == robusto_mt_halow ? 12U : 0U;
    uint8_t type = data[5] & 0x7fU;
    uint32_t packet_limit = CHUNK + 10U + extension;
    if (active_medium == robusto_mt_halow && peer == &receiver_view) {
        /* Model the receiver's independent default TX limit for feedback. */
        packet_limit = 1024U - ROBUSTO_HALOW_HEADER_SIZE;
    }
    assert(length <= packet_limit);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (trace_fail_request && type == FRAG_REQUEST) return ROB_ERR_SEND_FAIL;
#endif
    if (active_medium == robusto_mt_halow) {
        assert(data[5] & 0x80U);
        if (type == FRAG_MESSAGE) assert(!in_rx);
        if (type == FRAG_CHECK) assert(!busy_index_pending);
    }
    if (control_busy && type == control_type) {
        if (control_resume_at == 0) {
            assert(length <= sizeof(control_packet));
            memcpy(control_packet, data, length);
            control_length = length;
            control_resume_at = now + 650U;
        }
        assert(length == control_length && memcmp(control_packet, data, length) == 0);
        if (now < control_resume_at) {
            ++control_busy_checks;
            return ROB_ERR_QUEUE_FULL;
        }
        control_busy = false;
    }
    if (receipt) ++receipt_requests;
    if (recovery_scenario && type == FRAG_REQUEST) {
        memcpy(saved_request, data, sizeof(saved_request));
    }
    if (recovery_scenario && type == FRAG_CHECK) {
        memcpy(saved_check, data, sizeof(saved_check));
    }
    if (type == FRAG_MESSAGE) {
        assert(!receipt);
        ++fragment_attempts;
        uint32_t index;
        memcpy(&index, data + 6U + extension, sizeof(index));
        if (active_medium == robusto_mt_halow && (permanent_busy || now < busy_until)) {
            if (busy_index_pending) assert(index == busy_index);
            busy_index = index;
            busy_index_pending = true;
            ++busy_checks;
            return ROB_ERR_QUEUE_FULL;
        }
        if (busy_index_pending) {
            assert(index == busy_index);
            busy_index_pending = false;
        }
        if (index == 1 && one_shot_error != ROB_OK && !local_error_injected) {
            local_error_injected = true;
            return one_shot_error;
        }
        if (hard_failure && index == 1) return ROB_ERR_SEND_FAIL;
        if (apply_backpressure && burst_frames == 16U) {
            burst_frames = 0;
            ++busy_returns;
            return ROB_ERR_QUEUE_FULL;
        }
        ++burst_frames;
        ++data_fragments;
        if (recovery_scenario) {
            assert(index < 512);
            ++recovery_attempts[index];
            if (index == 0) {
                memcpy(saved_data, data, length);
                saved_data_length = length;
            }
            if (recovery_scenario == 1 && index == 1 && recovery_attempts[index] <= 2) {
                return ROB_OK;
            }
        }
        if (drop_fragment && index == 1) {
            drop_fragment = false;
            return ROB_OK;
        }
    }
    if (recovery_scenario == 1 && type == FRAG_RESEND) {
        ++feedback_packets;
        assert(length <= sizeof(previous_map));
        if (previous_map_length) enqueue_packet(peer, previous_map, previous_map_length);
        uint8_t map[530];
        memcpy(map, data, length);
        /* An invalid map must not consume the round. */
        map[20] = 2;
        enqueue_packet(peer, map, length);
        memcpy(map, data, length);
        /* Future-round feedback must not consume the current round either. */
        uint32_t round;
        memcpy(&round, map + 14, 4);
        ++round;
        memcpy(map + 14, &round, 4);
        enqueue_packet(peer, map, length);
        memcpy(map, data, length);
        if (previous_map_length) map[20] = 0; /* Previously acknowledged index 2. */
        for (unsigned duplicate = 0; duplicate < 8; ++duplicate) enqueue_packet(peer, map, length);
        memcpy(previous_map, data, length);
        previous_map_length = length;
        return ROB_OK;
    }
    if (track_completion_replies && type == FRAG_RESULT) {
        ++completion_replies;
        memcpy(saved_result, data, sizeof(saved_result));
    }
    if (recovery_scenario && type == FRAG_RESULT) {
        memcpy(saved_result, data, sizeof(saved_result));
        if (recovery_scenario == 2 && result_packets++ == 0) return ROB_OK;
    }
    enqueue_packet(peer, data, length);
    return ROB_OK;
}

void robusto_yield(void)
{
    if (!yield_recorded) {
        initial_burst_attempts = fragment_attempts;
        yield_recorded = true;
    }
    ++now;
    if (head != NULL) {
        packet_t *packet = head;
        head = packet->next;
        if (head == NULL) tail = &head;
        uint8_t type = packet->data[5] & 0x7fU;
        if (type == FRAG_MESSAGE) now += message_delay;
        if (type == FRAG_RESEND || type == FRAG_RESULT) now += feedback_delay;
        unsigned before = fragment_attempts;
        in_rx = true;
        handle_fragmented(packet->receiver_view, active_medium, packet->data,
                           packet->length,
                           active_medium == robusto_mt_halow ? ROBUSTO_HALOW_PAYLOAD_MAX - 22U : CHUNK,
                           send_packet);
        in_rx = false;
        if (active_medium == robusto_mt_halow) assert(fragment_attempts == before);
        free(packet);
    }
}

rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t length,
                                      robusto_peer_t *peer, e_media_type medium, int offset)
{
    assert(peer == &receiver_view && medium == active_medium && offset == 0);
    assert(length == MESSAGE_SIZE);
    assert(memcmp(data, expected, MESSAGE_SIZE) == 0);
    ++deliveries;
    free(data);
    return incoming_status;
}

#ifdef CROSS_MEDIA_FRAGMENT_CONTRACT
int main(void)
{
    static const e_media_type media[] = {
        robusto_mt_ble, robusto_mt_espnow, robusto_mt_lora,
        robusto_mt_i2c, robusto_mt_canbus, robusto_mt_mock
    };
    static const uint32_t chunks[] = {64U, 240U, 990U, 1460U};
    assert(init_crc32() == 0);
    for (unsigned i = 0; i < MAX_MESSAGE_SIZE; ++i) expected[i] = (uint8_t)(i * 17U);
    for (unsigned medium = 0; medium < sizeof(media) / sizeof(media[0]); ++medium) {
        active_medium = media[medium];
        for (unsigned size = 0; size < sizeof(chunks) / sizeof(chunks[0]); ++size) {
            chunk_size = chunks[size];
            message_size = CHUNK * 4U + 37U;
            for (unsigned trial = 0; trial < 4; ++trial) {
                robusto_message_fragment_init("cross-media-fragments");
                drop_fragment = trial == 1;
                one_shot_error = trial == 2 ? ROB_ERR_QUEUE_FULL :
                                 trial == 3 ? ROB_ERR_SEND_FAIL : ROB_OK;
                local_error_injected = yield_recorded = false;
                receipt_requests = data_fragments = fragment_attempts = 0;
                unsigned before = deliveries;
                assert(send_message_fragmented(&sender_view, active_medium, expected,
                                               MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
                assert(head == NULL && deliveries == before + 1);
                assert(receipt_requests == 1 && initial_burst_attempts == FRAGMENT_COUNT);
                assert(data_fragments == FRAGMENT_COUNT + (trial == 1 ? 1U : 0U));
                assert(local_error_injected == (trial >= 2));
                robusto_fragment_stats_t stats;
                robusto_fragment_stats_get(&stats, NULL);
                assert(stats.send_succeeded == 1 && stats.send_failed == 0);
                if (trial != 0) {
                    assert(stats.fragments_resent == 1 && stats.resend_request_received == 1);
                }
            }
        }
        printf("Shared fragmentation media=%u: four chunk sizes, CRC/reassembly, loss and local-error recovery passed\n",
               (unsigned)active_medium);
    }
    assert(deliveries == 96);
    cleanup_crc32();
    return 0;
}
#else
static void drain_packets(void)
{
    while (head != NULL) robusto_yield();
}

static void test_completion_probe_overlap(void)
{
    robusto_message_fragment_init("halow-completion-probe-overlap");
    unsigned before = deliveries;
    unmatched_feedback_warnings = completion_replies = 0;
    track_completion_replies = true;
    message_delay = 10;
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    drain_packets();
    message_delay = 0;
    assert(deliveries == before + 1 && completion_replies >= 2);
    assert(unmatched_feedback_warnings == 0);

    uint8_t altered[20];
    memcpy(altered, saved_result, sizeof(altered));
    altered[6] ^= 1U;
    enqueue_packet(&receiver_view, altered, sizeof(altered));
    memcpy(altered, saved_result, sizeof(altered));
    altered[0] ^= 1U;
    enqueue_packet(&receiver_view, altered, sizeof(altered));
    memcpy(altered, saved_result, sizeof(altered));
    altered[18] ^= 1U;
    enqueue_packet(&receiver_view, altered, sizeof(altered));
    memcpy(altered, saved_result, sizeof(altered));
    memset(altered + 14, 0xff, 4);
    enqueue_packet(&receiver_view, altered, sizeof(altered));
    drain_packets();
    assert(unmatched_feedback_warnings == 4);

    enqueue_packet(&sender_view, saved_result, sizeof(saved_result));
    drain_packets();
    assert(unmatched_feedback_warnings == 5);
    memcpy(altered, saved_result, sizeof(altered));
    altered[5] = FRAG_RESEND | 0x80U;
    altered[18] = 1;
    enqueue_packet(&receiver_view, altered, 19);
    drain_packets();
    assert(unmatched_feedback_warnings == 6);
    now += 2001U;
    enqueue_packet(&receiver_view, saved_result, sizeof(saved_result));
    drain_packets();
    assert(unmatched_feedback_warnings == 7);
    assert(deliveries == before + 1);
    track_completion_replies = false;
    puts("Completion/probe overlap: duplicate results recognized; wrong ID/hash/result/round/peer, resend and expired results remain warnings");
}

static void test_recovery_identity(void)
{
    robusto_message_fragment_init("halow-round-identity");
    unsigned before = deliveries;
    recovery_scenario = 1;
    memset(recovery_attempts, 0, sizeof(recovery_attempts));
    previous_map_length = feedback_packets = 0;
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    drain_packets();
    assert(deliveries == before + 1 && feedback_packets == 2);
    assert(recovery_attempts[1] == 3);
    for (unsigned i = 0; i < FRAGMENT_COUNT; ++i) {
        if (i != 1) assert(recovery_attempts[i] == 1);
    }
    uint8_t first_id[8], old_result[20], old_map[530];
    memcpy(first_id, saved_request + 6, sizeof(first_id));
    memcpy(old_result, saved_result, sizeof(old_result));
    memcpy(old_map, previous_map, previous_map_length);
    unsigned old_map_length = previous_map_length;
    /* Completed IDs replay their result, never the application payload. */
    now += 2500U;
    enqueue_packet(&sender_view, saved_request, sizeof(saved_request));
    enqueue_packet(&sender_view, saved_data, saved_data_length);
    enqueue_packet(&sender_view, saved_check, sizeof(saved_check));
    drain_packets();
    assert(deliveries == before + 1);

    /* No init/cache reset: identical bytes must create another real transfer. */
    recovery_scenario = 2;
    result_packets = 0;
    memset(recovery_attempts, 0, sizeof(recovery_attempts));
    enqueue_packet(&receiver_view, old_result, sizeof(old_result));
    enqueue_packet(&receiver_view, old_map, old_map_length);
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    drain_packets();
    assert(memcmp(first_id, saved_request + 6, sizeof(first_id)) != 0);
    assert(deliveries == before + 2 && result_packets >= 2);
    for (unsigned i = 0; i < FRAGMENT_COUNT; ++i) assert(recovery_attempts[i] == 1);
    recovery_scenario = 3;
    incoming_status = ROB_ERR_QUEUE_FULL;
    unsigned long started = now;
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_FAIL);
    drain_packets();
    assert(deliveries == before + 3 && now - started < 1000U);
    incoming_status = ROB_OK;
    enqueue_packet(&sender_view, saved_request, sizeof(saved_request));
    drain_packets();
    assert(deliveries == before + 3);
    recovery_scenario = 0;
    puts("Recovery IDs: duplicate/stale/future/invalid maps, repeated loss, monotonic ACKs, identical payloads, late packets and lost results passed");
}

static void test_credit_and_feedback_waits(void)
{
    for (int allocation = 0; allocation < 5; ++allocation) {
        robusto_message_fragment_init("halow-allocation-failure");
        fail_after = allocation;
        assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                       MESSAGE_SIZE, CHUNK, send_packet) != ROB_OK);
        fail_after = -1;
        while (head != NULL) {
            packet_t *packet = head;
            head = packet->next;
            free(packet->data);
            free(packet);
        }
        tail = &head;
    }
    unsigned before = deliveries;
    receipt_requests = data_fragments = busy_checks = 0;
    busy_until = (unsigned)now + 650U;
    robusto_message_fragment_init("halow-long-credits");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    assert(deliveries == before + 1 && receipt_requests == 1);
    assert(data_fragments == FRAGMENT_COUNT && busy_checks >= 600U);
    robusto_fragment_stats_t stats;
    robusto_fragment_stats_get(&stats, NULL);
    assert(stats.check_sent <= (FRAGMENT_COUNT > 250U ? 1U : 0U) &&
           stats.resend_request_received == 0);

    for (unsigned control = 0; control < 2; ++control) {
        before = deliveries;
        receipt_requests = data_fragments = control_busy_checks = 0;
        control_busy = true;
        control_resume_at = 0;
        control_type = control == 0 ? FRAG_REQUEST : FRAG_CHECK;
        drop_fragment = control != 0;
        robusto_message_fragment_init("halow-control-credits");
        assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                       MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
        assert(!control_busy && control_busy_checks == 650U);
        assert(deliveries == before + 1 && receipt_requests == 1);
        drain_packets();
    }

    before = deliveries;
    service_delay = 300;
    robusto_message_fragment_init("halow-slow-small-service");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    service_delay = 0;
    drain_packets();
    assert(deliveries == before + 1);

    before = deliveries;
    feedback_delay = 600;
    drop_fragment = true;
    robusto_message_fragment_init("halow-delayed-resend");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
    feedback_delay = 0;
    drain_packets();
    assert(deliveries == before + 1);

    permanent_busy = true;
    busy_checks = 0;
    unsigned long start = now;
    robusto_message_fragment_init("halow-starved-credits");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) != ROB_OK);
    assert(now - start == 30000U && busy_checks == 30000U);
    robusto_fragment_stats_get(&stats, NULL);
    assert(stats.check_sent == 0 && stats.send_timed_out == 1);
    permanent_busy = busy_index_pending = false;
    drain_packets();

    unsigned before_attempts = fragment_attempts;
    uint8_t *late = calloc(1, 6U + FRAGMENT_COUNT);
    assert(late != NULL);
    uint32_t hash = robusto_crc32(0, expected, MESSAGE_SIZE);
    memcpy(late, &hash, sizeof(hash));
    late[4] = MSG_FRAGMENTED;
    late[5] = FRAG_RESEND;
    in_rx = true;
    handle_fragmented(&sender_view, robusto_mt_halow, late, 6U + FRAGMENT_COUNT, CHUNK, send_packet);
    in_rx = false;
    assert(fragment_attempts == before_attempts);
}

int main(void)
{
    assert(init_crc32() == 0);
    if (ROBUSTO_HALOW_TX_FRAME_MAX == 256U) {
        assert(CHUNK == 214U && FRAGMENT_COUNT == 258U && MESSAGE_SIZE % CHUNK == 4U);
    }
    if (ROBUSTO_HALOW_TX_FRAME_MAX == 1642U) {
        assert(CHUNK == 1600U && FRAGMENT_COUNT == 35U && MESSAGE_SIZE % CHUNK == 602U);
    }
    for (unsigned i = 0; i < MESSAGE_SIZE; ++i) expected[i] = (uint8_t)(i * 17U);
    for (unsigned trial = 0; trial < 3; ++trial) {
        robusto_message_fragment_init("halow-fragments");
        drop_fragment = trial == 1;
        apply_backpressure = trial == 2;
        burst_frames = busy_returns = 0;
        receipt_requests = data_fragments = 0;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        unsigned before_dumps = trace_dumps;
#endif
        assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                       MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        assert(trace_dumps == before_dumps + 2);
#endif
        drain_packets();
        assert(head == NULL);
        assert(receipt_requests == 1);
        if (trial < 2) assert(data_fragments == FRAGMENT_COUNT + (trial == 1 ? 1U : 0U));
        if (trial == 2) assert(busy_returns >= 2U);
        robusto_fragment_stats_t stats;
        robusto_fragment_stats_get(&stats, NULL);
        assert(stats.send_succeeded == 1 && stats.send_failed == 0);
        if (trial == 2) {
            assert(data_fragments == FRAGMENT_COUNT);
            assert(stats.resend_request_received == 0 && stats.check_sent == 0);
        }
        if (trial == 1) {
            assert(stats.fragments_resent == 1 && stats.resend_request_received == 1);
        }
    }
    assert(deliveries == 3);
    assert(small_service_calls > 0);
    apply_backpressure = false;
    test_recovery_identity();
    test_credit_and_feedback_waits();
    test_completion_probe_overlap();
    hard_failure = true;
    fragment_attempts = data_fragments = 0;
    robusto_message_fragment_init("halow-hard-failure");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) != ROB_OK);
    assert(fragment_attempts == 2 && data_fragments == 1);
    robusto_fragment_stats_t stats;
    robusto_fragment_stats_get(&stats, NULL);
    assert(stats.send_failed == 1);
    while (head != NULL) {
        packet_t *packet = head;
        head = packet->next;
        free(packet->data);
        free(packet);
    }
    tail = &head;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    unsigned before_dumps = trace_dumps;
    trace_fail_request = true;
    fragment_attempts = 0;
    robusto_message_fragment_init("halow-trace-request-failure");
    assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                   MESSAGE_SIZE, CHUNK, send_packet) == ROB_ERR_SEND_FAIL);
    assert(trace_dumps == before_dumps + 1 && fragment_attempts == 0 && head == NULL);
    trace_fail_request = false;
#endif
    cleanup_crc32();
    puts("HaLow 55KB transfer, same-fragment credit resume, worker-only resends, delayed feedback and 30s starvation bound passed");
    return 0;
}
#endif
