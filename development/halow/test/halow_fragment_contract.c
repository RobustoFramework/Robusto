#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <robusto_halow.h>
#include <robusto_incoming.h>
#include <robusto_message.h>
#include <robusto_network_init.h>
#include <robusto_peer.h>
#include <robusto_qos.h>
#include <robusto_system.h>
#include <robusto_time.h>
#include "compat_crc32.h"

#define CHUNK (ROBUSTO_HALOW_PAYLOAD_MAX - 10U)
#define MESSAGE_SIZE (CHUNK * 4U + 37U)

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
static uint8_t expected[MESSAGE_SIZE];
static unsigned deliveries;
static unsigned receipt_requests;
static unsigned data_fragments;
static bool drop_fragment;
static unsigned long now;

uint32_t robusto_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    return crc32_be_compat(crc, data, length);
}
void *robusto_malloc(size_t length) { return malloc(length); }
void robusto_free(void *data) { free(data); }
unsigned long r_millis(void) { return now; }
unsigned long r_micros(void) { return now * 1000; }
void r_delay(unsigned long delay) { now += delay; }
void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...)
{
    (void)level; (void)tag; (void)format;
}
void rob_log_bit_mesh(rob_log_level_t level, const char *prefix, const uint8_t *data, int length)
{
    (void)level; (void)prefix; (void)data; (void)length;
}
robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type medium)
{
    assert(medium == robusto_mt_halow);
    return &peer->halow_info;
}
void add_to_history(robusto_media_t *info, bool sending, rob_ret_val_t result)
{
    (void)info; (void)sending; (void)result;
}

static rob_ret_val_t send_packet(robusto_peer_t *peer, uint8_t *data, uint32_t length, bool receipt)
{
    assert(length <= ROBUSTO_HALOW_PAYLOAD_MAX);
    if (receipt) ++receipt_requests;
    if (data[ROBUSTO_CRC_LENGTH + 1] == FRAG_MESSAGE) {
        assert(!receipt);
        ++data_fragments;
        uint32_t index;
        memcpy(&index, data + ROBUSTO_CRC_LENGTH + 2, sizeof(index));
        if (drop_fragment && index == 1) {
            drop_fragment = false;
            return ROB_OK;
        }
    }
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
    return ROB_OK;
}

void robusto_yield(void)
{
    ++now;
    if (head != NULL) {
        packet_t *packet = head;
        head = packet->next;
        if (head == NULL) tail = &head;
        handle_fragmented(packet->receiver_view, robusto_mt_halow, packet->data,
                           packet->length, CHUNK, send_packet);
        free(packet);
    }
}

rob_ret_val_t robusto_handle_incoming(uint8_t *data, uint32_t length,
                                      robusto_peer_t *peer, e_media_type medium, int offset)
{
    assert(peer == &receiver_view && medium == robusto_mt_halow && offset == 0);
    assert(length == MESSAGE_SIZE);
    assert(memcmp(data, expected, MESSAGE_SIZE) == 0);
    ++deliveries;
    free(data);
    return ROB_OK;
}

int main(void)
{
    assert(init_crc32() == 0);
    for (unsigned i = 0; i < MESSAGE_SIZE; ++i) expected[i] = (uint8_t)(i * 17U);
    for (unsigned trial = 0; trial < 2; ++trial) {
        robusto_message_fragment_init("halow-fragments");
        drop_fragment = trial == 1;
        receipt_requests = data_fragments = 0;
        assert(send_message_fragmented(&sender_view, robusto_mt_halow, expected,
                                       MESSAGE_SIZE, CHUNK, send_packet) == ROB_OK);
        assert(head == NULL);
        assert(receipt_requests == 1);
        assert(data_fragments == (trial == 0 ? 5U : 6U));
        robusto_fragment_stats_t stats;
        robusto_fragment_stats_get(&stats, NULL);
        assert(stats.send_succeeded == 1 && stats.send_failed == 0);
        if (trial == 1) {
            assert(stats.fragments_resent == 1 && stats.resend_request_received == 1);
        }
    }
    assert(deliveries == 2);
    cleanup_crc32();
    puts("HaLow production fragmentation and missing-fragment recovery passed");
    return 0;
}
