/* Host contract for the production presentation serializer/parser (robusto_presentation.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <robusto_message.h>
#include <robusto_peer.h>
#include <robusto_queue.h>
#include <robusto_qos.h>
#include <robusto_media.h>
#include <robusto_concurrency.h>
#include <robusto_logging.h>
#include <robusto_time.h>
#include <robusto_pubsub_server.h>
#include <robusto_network_service.h>
#include "compat_crc32.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
#include <robusto_halow.h>
#endif

int robusto_make_presentation(robusto_peer_t *peer, uint8_t **msg, bool is_reply, e_presentation_reason reason);

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const uint8_t HOST_MAC[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
static const uint8_t PEER_MAC[6] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
static const uint8_t RADIO_MAC[6] = {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5};
#endif
#define RELATION_ID 0x11223344U

static robusto_peer_t host_peer;
static robusto_media_types host_media = robusto_mt_espnow;
static robusto_peer_t *existing_peer_result = NULL;
static int deleted_handle = -1;
static int add_relation_calls = 0;
static robusto_media_types last_relation_media = 0;
static int init_media_calls = 0;
static int send_calls = 0;
static bool fail_queue_lock_once;
static bool use_pubsub_media, use_psram;
static unsigned psram_allocations;
static e_media_type pubsub_media;
static bool generic_policy, generic_busy;
static unsigned generic_releases, generic_preallocations, generic_raw_admissions;
static rob_ret_val_t local_send_result = ROB_OK;
static unsigned local_send_calls;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
static bool last_relation_halow_valid = false;
static uint8_t last_relation_halow_mac[6];
static unsigned large_begin_calls, large_end_calls;
#endif
static robusto_media_t dummy_media;
static bool track_allocations;
static struct { void *pointer; bool live; } allocations[256];
static unsigned allocation_count;
static unsigned free_count;
static unsigned long now_ms;
static bool queue_sends;
static bool complete_on_yield;
static bool complete_at_timeout;
static bool draining;
static int fail_after = -1;
static bool timeout_during_send;
static queue_state *active_state;
static bool drain_on_unlock;
static queue_context_t send_queue;
static STAILQ_HEAD(contract_queue, media_queue_item) pending;
static void drain_one(void);

bool robusto_has_spiram(void) { return use_psram; }
void *robusto_spi_malloc(size_t size) { ++psram_allocations; return robusto_malloc(size); }
uint32_t robusto_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    return crc32_be_compat(crc, data, length);
}
uint8_t robusto_encode_message_context(message_context_t *context)
{
    CHECK(context->message_type == MSG_MESSAGE && context->is_service_call && context->has_binary);
    return MSG_MESSAGE;
}
mutex_ref_t robusto_mutex_init(void) { return (mutex_ref_t)&send_queue; }
void robusto_mutex_deinit(mutex_ref_t mutex) { (void)mutex; }
void robusto_register_on_delete_peer(callback_delete_peer_t *callback) { (void)callback; }
rob_ret_val_t robusto_register_network_service(network_service_t *service) { (void)service; return ROB_OK; }

/* Stubs for the framework dependencies of robusto_presentation.c */
robusto_peer_t *get_host_peer() { return &host_peer; }
robusto_media_types get_host_supported_media_types() { return host_media; }
uint32_t calc_relation_id(uint8_t *mac_1, uint8_t *mac_2) { (void)mac_1; (void)mac_2; return RELATION_ID; }
void *robusto_malloc(size_t size)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    void *pointer = malloc(size);
    memset(pointer, 0xA5, size);
    if (track_allocations) {
        CHECK(allocation_count < 256);
        allocations[allocation_count].pointer = pointer;
        allocations[allocation_count++].live = true;
    }
    return pointer;
}
char *media_type_to_str(robusto_media_types media_type) { (void)media_type; return "mt"; }
void rob_log_bit_mesh(rob_log_level_t level, const char *tag, const uint8_t *data, int data_len) { (void)level; (void)tag; (void)data; (void)data_len; }
void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...) { (void)level; (void)tag; (void)format; }
robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type media_type) { (void)peer; (void)media_type; return &dummy_media; }
void set_state(robusto_peer_t *peer, robusto_media_t *info, e_media_type media_type, e_media_state media_state, e_media_problem problem) { (void)peer; (void)info; (void)media_type; (void)media_state; (void)problem; }
void init_supported_media_types(robusto_peer_t *peer) { (void)peer; init_media_calls++; }
void log_peer_info(char *_log_prefix, robusto_peer_t *peer) { (void)_log_prefix; (void)peer; }
bool notify_on_new_peer(robusto_peer_t *peer) { (void)peer; return false; }
robusto_peer_t *robusto_peers_find_duplicate_by_base_mac_address(robusto_peer_t *check_peer) { (void)check_peer; return existing_peer_result; }
void robusto_free(void *ptr)
{
    if (track_allocations && ptr != NULL) {
        bool found = false;
        for (unsigned i = allocation_count; i > 0; --i) {
            if (allocations[i - 1].pointer == ptr && allocations[i - 1].live) {
                allocations[i - 1].live = false;
                ++free_count;
                found = true;
                break;
            }
        }
        CHECK(found);
    }
    free(ptr);
}
unsigned long r_micros() { return 0; }
static e_msg_type_t last_envelope_type = MSG_HEARTBEAT;
/* Envelope building is a separate production unit; capture the presentation payload verbatim. */
int robusto_make_multi_message_internal(e_msg_type_t message_type, uint16_t service_id, uint16_t conversation_id,
                                        uint8_t *strings_data, uint32_t strings_length,
                                        uint8_t *binary_data, uint32_t binary_length, uint8_t **dest_message)
{
    (void)service_id; (void)conversation_id; (void)strings_data; (void)strings_length;
    last_envelope_type = message_type;
    *dest_message = robusto_malloc(binary_length);
    if (*dest_message == NULL) return ROB_ERR_OUT_OF_MEMORY;
    memcpy(*dest_message, binary_data, binary_length);
    return (int)binary_length;
}
int robusto_peers_delete_peer(uint16_t peer_handle) { deleted_handle = peer_handle; return 0; }
rob_ret_val_t robusto_create_task(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity) { (void)task_function; (void)parameter; (void)task_name; (void)handle; (void)affinity; return ROB_OK; }
rob_ret_val_t robusto_delete_current_task() { return ROB_OK; }
bool robusto_waitfor_byte(uint8_t *byte_ptr, uint8_t value, uint32_t timeout_ms) { (void)timeout_ms; return *byte_ptr == value; }
void r_delay(unsigned long milliseconds) { (void)milliseconds; }
unsigned long r_millis(void) { return now_ms; }
void robusto_yield(void)
{
    if (!draining) now_ms += complete_at_timeout ? 6000 : 1;
    if (complete_on_yield) {
        complete_on_yield = false;
        drain_one();
    }
}
rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout)
{
    (void)mutex; (void)timeout;
    if (fail_queue_lock_once) {
        fail_queue_lock_once = false;
        return ROB_ERR_MUTEX;
    }
    return ROB_OK;
}
rob_ret_val_t robusto_mutex_give(mutex_ref_t mutex)
{
    (void)mutex;
    if (drain_on_unlock && send_queue.count != 0) {
        drain_on_unlock = false;
        drain_one();
    }
    return ROB_OK;
}
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
rob_ret_val_t robusto_halow_begin_large_send(robusto_peer_t *peer)
{
    ++large_begin_calls;
    if (peer->halow_large_send_pending) return ROB_ERR_QUEUE_FULL;
    peer->halow_large_send_pending = true;
    return ROB_OK;
}
void robusto_halow_end_large_send(robusto_peer_t *peer)
{
    ++large_end_calls;
    peer->halow_large_send_pending = false;
}
static rob_ret_val_t admit_halow(void *owner, uint32_t length, bool before_allocation)
{
    if (length <= ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX) return ROB_OK;
    if (before_allocation && !robusto_send_queue_accepts_large_message(robusto_mt_halow)) {
        return ROB_ERR_QUEUE_FULL;
    }
    return robusto_halow_begin_large_send(owner);
}
static void release_halow(void *owner, uint32_t length)
{
    if (length > ROBUSTO_PREFIX_BYTES + ROBUSTO_HALOW_TX_PAYLOAD_MAX) {
        robusto_halow_end_large_send(owner);
    }
}
rob_ret_val_t robusto_halow_begin_presentation(robusto_peer_t *peer, uint8_t mask)
{
    if (peer->halow_pending_presentations & mask) return ROB_ERR_NOT_READY;
    peer->halow_pending_presentations |= mask;
    return ROB_OK;
}
void robusto_halow_end_presentation(robusto_peer_t *peer, uint8_t mask)
{
    peer->halow_pending_presentations &= (uint8_t)~mask;
}
#endif
static rob_ret_val_t admit_generic(void *owner, uint32_t length, bool before_allocation)
{
    CHECK(owner != NULL);
    if (length <= 64) return ROB_OK;
    if (before_allocation) ++generic_preallocations;
    else ++generic_raw_admissions;
    if (generic_busy) return ROB_ERR_QUEUE_FULL;
    generic_busy = true;
    return ROB_OK;
}
static void release_generic(void *owner, uint32_t length)
{
    CHECK(owner != NULL);
    if (length <= 64) return;
    CHECK(generic_busy);
    generic_busy = false;
    ++generic_releases;
}
queue_context_t *mock_get_queue_context(void)
{
    ++send_calls;
    send_queue.on_admit_cb = generic_policy ? admit_generic : NULL;
    send_queue.on_release_cb = generic_policy ? release_generic : NULL;
    return queue_sends ? &send_queue : NULL;
}
#ifdef PRESENTATION_CONTRACT_ALL_MEDIA
queue_context_t *ble_get_queue_context(void) { return mock_get_queue_context(); }
queue_context_t *espnow_get_queue_context(void) { return mock_get_queue_context(); }
queue_context_t *lora_get_queue_context(void) { return mock_get_queue_context(); }
queue_context_t *i2c_get_queue_context(void) { return mock_get_queue_context(); }
queue_context_t *canbus_get_queue_context(void) { return mock_get_queue_context(); }
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
queue_context_t *halow_get_queue_context(void)
{
    queue_context_t *queue = mock_get_queue_context();
    if (queue != NULL) {
        queue->on_admit_cb = admit_halow;
        queue->on_release_cb = release_halow;
    }
    return queue;
}
#endif
rob_ret_val_t set_suitable_media(robusto_peer_t *peer, uint16_t length, robusto_media_types exclude, e_media_type *result)
{
    (void)peer; (void)length; (void)exclude; (void)result;
    if (use_pubsub_media) {
        *result = pubsub_media;
        return ROB_OK;
    }
    CHECK(false); /* HI/HIR has no receipt and must never fall back to other media. */
    return ROB_ERR_NOT_SUPPORTED;
}
void add_to_history(robusto_media_t *info, bool sending, rob_ret_val_t result)
{
    (void)info; (void)sending; (void)result;
}
static rob_ret_val_t local_send(robusto_peer_t *peer, uint8_t *data, uint32_t length, bool receipt)
{
    (void)peer; (void)data; (void)length;
    CHECK(!receipt || use_pubsub_media);
    ++local_send_calls;
    if (generic_policy && length > 64) CHECK(generic_busy);
    if (timeout_during_send) {
        timeout_during_send = false;
        CHECK((*active_state)[0] == QUEUE_STATE_RUNNING);
        unsigned freed = free_count;
        robusto_free_queue_state(active_state);
        CHECK((*active_state)[0] == QUEUE_STATE_DROPPED && free_count == freed);
    }
    return local_send_result;
}
static void *first_pending(queue_context_t *context) { (void)context; return STAILQ_FIRST(&pending); }
static void insert_pending(queue_context_t *context, void *item)
{
    STAILQ_INSERT_TAIL(&pending, (media_queue_item_t *)item, items);
    ++context->count;
}
static void drain_one(void)
{
    media_queue_item_t *item = STAILQ_FIRST(&pending);
    CHECK(item != NULL);
    STAILQ_REMOVE_HEAD(&pending, items);
    --send_queue.count;
    robusto_peer_t *peer = item->peer;
    uint8_t mask = item->presentation_mask;
    active_state = item->state;
    draining = true;
    send_work_item(item, &dummy_media, robusto_mt_mock, local_send, NULL, &send_queue);
    draining = false;
    active_state = NULL;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    robusto_halow_end_presentation(peer, mask);
#else
    (void)mask;
#endif
    peer->state = PEER_KNOWN_INSECURE;
}
bool add_relation(uint8_t *mac_address, uint32_t relation_id_incoming, uint32_t relation_id_outgoing, robusto_media_types supported_media_types
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
                  , uint8_t i2c_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
                  , uint8_t canbus_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
                  , const rob_mac_address *halow_mac_address, bool halow_address_valid
#endif
)
{
    (void)mac_address; (void)relation_id_incoming; (void)relation_id_outgoing;
    add_relation_calls++;
    last_relation_media = supported_media_types;
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    (void)i2c_address;
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    (void)canbus_address;
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    last_relation_halow_valid = halow_address_valid;
    memcpy(last_relation_halow_mac, halow_mac_address, 6);
#endif
    return true;
}

static void reset_peer(robusto_peer_t *peer, uint16_t handle)
{
    memset(peer, 0, sizeof(*peer));
    peer->peer_handle = handle;
    peer->state = PEER_UNKNOWN;
}

static void reset_counters(void)
{
    existing_peer_result = NULL;
    deleted_handle = -1;
    add_relation_calls = 0;
    last_relation_media = 0;
    init_media_calls = 0;
}

/* Builds a presentation like a remote host would; version 1 inserts the high mask byte at offset 17. */
static uint32_t build(uint8_t *out, uint8_t type, uint8_t version, robusto_media_types media, const char *name)
{
    uint32_t pos = 0;
    out[pos++] = type;
    out[pos++] = version;
    out[pos++] = 0;
    out[pos++] = (uint8_t)(media & 0xFFU);
    out[pos++] = 0x21;
    out[pos++] = 0x22;
    out[pos++] = presentation_add;
    uint32_t rel = 0x55667788U;
    memcpy(out + pos, &rel, 4);
    pos += 4;
    memcpy(out + pos, PEER_MAC, 6);
    pos += 6;
    if (version == 1)
    {
        out[pos++] = (uint8_t)(media >> 8);
    }
    size_t name_len = strlen(name);
    memcpy(out + pos, name, name_len + 1);
    return pos + (uint32_t)name_len + 1U;
}

static rob_ret_val_t parse(robusto_peer_t *peer, uint8_t *data, uint32_t length)
{
    robusto_message_t message;
    memset(&message, 0, sizeof(message));
    message.peer = peer;
    message.media_type = robusto_mt_espnow;
    message.binary_data = data;
    message.binary_data_length = length;
    message.raw_data = data;
    message.raw_data_length = length;
    return robusto_handle_presentation(&message);
}

static void test_make_legacy_v0_byte_for_byte(void)
{
    robusto_peer_t peer;
    reset_peer(&peer, 1);
    memcpy(peer.base_mac_address, PEER_MAC, 6);
    peer.supported_media_types = robusto_mt_espnow;
    host_media = robusto_mt_espnow | robusto_mt_ble
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
                 | robusto_mt_halow
#endif
        ;
    uint8_t *msg = NULL;
    int len = robusto_make_presentation(&peer, &msg, true, presentation_reply);
    const char *name = host_peer.name;
    uint8_t expected[64];
    uint32_t pos = 0;
    expected[pos++] = NET_HIR;
    expected[pos++] = 0;
    expected[pos++] = ROBUSTO_PROTOCOL_VERSION_MIN;
    expected[pos++] = robusto_mt_espnow | robusto_mt_ble;
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    expected[pos++] = CONFIG_I2C_ADDR;
#else
    expected[pos++] = 0;
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    expected[pos++] = CONFIG_ROBUSTO_CANBUS_ADDRESS;
#else
    expected[pos++] = 0;
#endif
    expected[pos++] = presentation_reply;
    uint32_t rel = RELATION_ID;
    memcpy(expected + pos, &rel, 4);
    pos += 4;
    memcpy(expected + pos, HOST_MAC, 6);
    pos += 6;
    memcpy(expected + pos, name, strlen(name) + 1);
    pos += (uint32_t)strlen(name) + 1U;
    CHECK(len == (int)pos);
    CHECK(msg != NULL && memcmp(msg, expected, pos) == 0);
    CHECK(peer.relation_id_incoming == RELATION_ID);
    CHECK(last_envelope_type == MSG_NETWORK);
    free(msg);
}

static void test_make_v1_for_halow_peer(void)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    robusto_peer_t peer;
    reset_peer(&peer, 1);
    memcpy(peer.base_mac_address, PEER_MAC, 6);
    peer.supported_media_types = robusto_mt_halow;
    host_media = robusto_mt_espnow | robusto_mt_halow;
    uint8_t *msg = NULL;
    int len = robusto_make_presentation(&peer, &msg, false, presentation_add);
    size_t name_len = strlen(host_peer.name);
    CHECK(len == (int)(18U + name_len + 1U));
    CHECK(msg[0] == NET_HI);
    CHECK(msg[1] == 1U);
    CHECK(msg[3] == robusto_mt_espnow);
    CHECK(memcmp(msg + 11, HOST_MAC, 6) == 0);
    CHECK(msg[17] == 0x01U);
    CHECK(memcmp(msg + 18, host_peer.name, name_len + 1U) == 0);

    /* Round trip through the production parser. */
    robusto_peer_t receiver;
    reset_peer(&receiver, 7);
    reset_counters();
    msg[0] = NET_HIR;
    CHECK(parse(&receiver, msg, (uint32_t)len) == ROB_OK);
    CHECK(receiver.supported_media_types == (robusto_mt_espnow | robusto_mt_halow));
    CHECK(strcmp(receiver.name, host_peer.name) == 0);
    CHECK(receiver.protocol_version == 1U);
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    CHECK(receiver.i2c_address == CONFIG_I2C_ADDR);
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    CHECK(receiver.canbus_address == CONFIG_ROBUSTO_CANBUS_ADDRESS);
#endif
    free(msg);
#endif
}

static void test_parse_v0_legacy(void)
{
    robusto_peer_t peer;
    reset_peer(&peer, 2);
    reset_counters();
    uint8_t data[64];
    uint32_t len = build(data, NET_HIR, 0, robusto_mt_espnow | robusto_mt_mock, "legacy");
    CHECK(len == 24U);
    CHECK(parse(&peer, data, len) == ROB_OK);
    CHECK(peer.supported_media_types == (robusto_mt_espnow | robusto_mt_mock));
    CHECK(strcmp(peer.name, "legacy") == 0);
    CHECK(memcmp(peer.base_mac_address, PEER_MAC, 6) == 0);
    CHECK(peer.relation_id_outgoing == 0x55667788U);
    CHECK(peer.state == PEER_KNOWN_INSECURE);
    CHECK(add_relation_calls == 1);
    CHECK(last_relation_media == (robusto_mt_espnow | robusto_mt_mock));
    CHECK(init_media_calls == 1);
}

static void test_parse_v1_high_bit(void)
{
    robusto_peer_t peer;
    reset_peer(&peer, 3);
    reset_counters();
    uint8_t data[64];
    uint32_t len = build(data, NET_HIR, 1, robusto_mt_espnow | robusto_mt_halow, "halow-peer");
    CHECK(len == 29U);
    CHECK(parse(&peer, data, len) == ROB_OK);
    CHECK(peer.supported_media_types == (robusto_mt_espnow | robusto_mt_halow));
    CHECK(strcmp(peer.name, "halow-peer") == 0);
    CHECK(last_relation_media == (robusto_mt_espnow | robusto_mt_halow));
}

static void test_halow_mapping_preserved(void)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    /* A v0 presentation (no high byte) must not drop a bound HaLow mapping. */
    robusto_peer_t peer;
    reset_peer(&peer, 4);
    reset_counters();
    memcpy(peer.halow_mac_address, RADIO_MAC, 6);
    peer.halow_address_valid = true;
    peer.supported_media_types = robusto_mt_halow;
    uint8_t data[64];
    uint32_t len = build(data, NET_HIR, 0, robusto_mt_espnow, "bound");
    CHECK(parse(&peer, data, len) == ROB_OK);
    CHECK(peer.supported_media_types == (robusto_mt_espnow | robusto_mt_halow));
    CHECK(peer.halow_address_valid);
    CHECK(memcmp(peer.halow_mac_address, RADIO_MAC, 6) == 0);
    CHECK(last_relation_halow_valid);
    CHECK(memcmp(last_relation_halow_mac, RADIO_MAC, 6) == 0);

    /* Duplicate base-MAC merge transfers the mapping before the duplicate is deleted. */
    robusto_peer_t existing;
    robusto_peer_t duplicate;
    reset_peer(&existing, 10);
    reset_peer(&duplicate, 11);
    reset_counters();
    memcpy(duplicate.halow_mac_address, RADIO_MAC, 6);
    duplicate.halow_address_valid = true;
    existing_peer_result = &existing;
    len = build(data, NET_HIR, 1, robusto_mt_halow, "merged");
    CHECK(parse(&duplicate, data, len) == ROB_OK);
    CHECK(deleted_handle == 11);
    CHECK(existing.halow_address_valid);
    CHECK(memcmp(existing.halow_mac_address, RADIO_MAC, 6) == 0);
    CHECK(existing.supported_media_types == robusto_mt_halow);
    CHECK(strcmp(existing.name, "merged") == 0);
    existing_peer_result = NULL;
#endif
}

static void test_malformed_rejected(void)
{
    robusto_peer_t peer;
    uint8_t data[64];
    uint32_t len;
    reset_counters();

    CHECK(robusto_handle_presentation(NULL) == ROB_FAIL);

    reset_peer(&peer, 5);
    len = build(data, NET_HIR, 0, robusto_mt_espnow, "x");
    CHECK(parse(&peer, data, 17) == ROB_FAIL);              /* header only */
    CHECK(parse(&peer, NULL, len) == ROB_FAIL);             /* missing data */

    len = build(data, NET_HIR, 2, robusto_mt_espnow, "x");
    CHECK(parse(&peer, data, len) == ROB_FAIL);             /* unsupported version */

    len = build(data, 0x7F, 0, robusto_mt_espnow, "x");
    CHECK(parse(&peer, data, len) == ROB_FAIL);             /* not HI/HIR */

    len = build(data, NET_HIR, 0, robusto_mt_espnow, "");
    CHECK(parse(&peer, data, len) == ROB_FAIL);             /* empty name */

    len = build(data, NET_HIR, 0, robusto_mt_espnow, "abc");
    CHECK(parse(&peer, data, len - 1U) == ROB_FAIL);        /* unterminated name */
    data[len] = 'z';
    CHECK(parse(&peer, data, len + 1U) == ROB_FAIL);        /* trailing bytes after terminator */

    len = build(data, NET_HIR, 0, robusto_mt_espnow, "0123456789abcdef");
    CHECK(parse(&peer, data, len) == ROB_FAIL);             /* 16 chars does not fit 16-byte name */
    len = build(data, NET_HIR, 0, robusto_mt_espnow, "0123456789abcde");
    CHECK(parse(&peer, data, len) == ROB_OK);               /* 15 chars + terminator fits */

    len = build(data, NET_HIR, 1, robusto_mt_halow, "x");
    CHECK(parse(&peer, data, 18) == ROB_FAIL);              /* v1 without name */
    CHECK(parse(&peer, data, 19) == ROB_FAIL);              /* v1 name not terminated */
    CHECK(add_relation_calls == 1);
}

static void test_queue_state_ownership(void)
{
    struct { queue_state state; uint8_t canary[2]; } storage = {{0}, {0xA7, 0xB8}};
    robusto_set_queue_state_result(&storage.state, ROB_ERR_SEND_FAIL);
    CHECK(storage.canary[0] == 0xA7 && storage.canary[1] == 0xB8);
    rob_ret_val_t result = ROB_OK;
    CHECK(!robusto_waitfor_queue_state(&storage.state, 6000, &result));
    CHECK(result == ROB_ERR_SEND_FAIL);
    robusto_set_queue_state_result(&storage.state, ROB_OK);
    CHECK(robusto_waitfor_queue_state(&storage.state, 6000, &result));
    CHECK(result == ROB_OK);

    queue_state *state = robusto_malloc(sizeof(*state));
    robusto_set_queue_state_queued_on_ok(state, ROB_OK);
    robusto_free_queue_state(state);
    CHECK((*state)[0] == QUEUE_STATE_DROPPED);
    robusto_set_queue_state_running(state);
    CHECK((*state)[0] == QUEUE_STATE_DROPPED);
    robusto_set_queue_state_trying(state);
    CHECK((*state)[0] == QUEUE_STATE_DROPPED);
    unsigned freed = free_count;
    robusto_set_queue_state_result(state, ROB_OK);
    CHECK(free_count == freed + 1);
}

static void test_pending_presentations(void)
{
    track_allocations = true;
    test_queue_state_ownership();
    queue_sends = true;
    memset(&send_queue, 0, sizeof(send_queue));
    STAILQ_INIT(&pending);
    send_queue.normal_max_count = 3;
    send_queue.important_max_count = 5;
    send_queue.watchdog_timeout = 10;
    send_queue.blocked = true;
    send_queue.first_queue_item_cb = first_pending;
    send_queue.insert_tail_cb = insert_pending;
    robusto_peer_t peer;
    reset_peer(&peer, 1);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    host_media = robusto_mt_halow;
    peer.supported_media_types = host_media;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_ERR_TIMEOUT);
    CHECK(send_queue.count == 1);
    media_queue_item_t *original = STAILQ_FIRST(&pending);
    CHECK(original->important && original->queue_item_type == media_qit_recovery);
    CHECK((*original->state)[0] == QUEUE_STATE_DROPPED);
    unsigned allocated = allocation_count;
    peer.relation_id_incoming = 0x98765432;
    for (unsigned i = 0; i < 12; ++i) {
        CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_ERR_NOT_READY);
        CHECK(send_queue.count == 1 && STAILQ_FIRST(&pending) == original);
        CHECK(peer.relation_id_incoming == 0x98765432);
        CHECK(allocation_count == allocated);
    }
    uint8_t *ordinary = robusto_malloc(8);
    CHECK(send_message_raw(&peer, robusto_mt_halow, ordinary, 8, NULL, false) == ROB_OK);
    CHECK(STAILQ_NEXT(original, items)->presentation_mask == 0);
    /* Replies, different reasons and other peers must remain independent. */
    CHECK(robusto_send_presentation(&peer, host_media, true, presentation_reply) == ROB_ERR_TIMEOUT);
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_add) == ROB_ERR_TIMEOUT);
    robusto_peer_t other;
    reset_peer(&other, 2);
    other.supported_media_types = host_media;
    CHECK(robusto_send_presentation(&other, host_media, false, presentation_recover) == ROB_ERR_TIMEOUT);
    CHECK(send_queue.count == 5);
    /* A rejected reply must not release the request reservation or leak data. */
    CHECK(robusto_send_presentation(&peer, host_media, true, presentation_reply) == ROB_ERR_QUEUE_FULL);
    CHECK(peer.halow_pending_presentations == ((1U << presentation_recover) | (1U << presentation_add)));
    set_queue_blocked(&send_queue, false);
    while (send_queue.count != 0) drain_one();
    CHECK(peer.halow_pending_presentations == 0 && other.halow_pending_presentations == 0);
    peer.state = PEER_UNKNOWN;
    complete_on_yield = true;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_OK);
    CHECK(send_queue.count == 0 && peer.halow_pending_presentations == 0);

    /* Queue completion at the exact timeout boundary must still release once. */
    peer.state = PEER_UNKNOWN;
    complete_on_yield = complete_at_timeout = true;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_OK);
    complete_at_timeout = false;

    /* Worker may finish after admission unlocks, before enqueue returns. */
    peer.state = PEER_UNKNOWN;
    drain_on_unlock = true;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_OK);
    CHECK(!drain_on_unlock && peer.halow_pending_presentations == 0);

    send_queue.count = send_queue.important_max_count;
    peer.state = PEER_UNKNOWN;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_ERR_QUEUE_FULL);
    CHECK(peer.halow_pending_presentations == 0);
    send_queue.count = 0;

    for (int i = 0; i < 4; ++i) {
        peer.state = PEER_UNKNOWN;
        fail_after = i; /* state, construction buffer, envelope, queue item */
        CHECK(robusto_send_presentation(&peer, host_media, false, presentation_recover) == ROB_ERR_OUT_OF_MEMORY);
        fail_after = -1;
        CHECK(peer.halow_pending_presentations == 0 && send_queue.count == 0);
        CHECK(allocation_count == free_count);
    }

    /* Caller timeout after dequeue, while the worker still owns the state. */
    const uint8_t mask = 1U << presentation_recover;
    CHECK(robusto_halow_begin_presentation(&peer, mask) == ROB_OK);
    queue_state *state = robusto_malloc(sizeof(*state));
    uint8_t *payload = robusto_malloc(8);
    CHECK(send_presentation_raw_internal(&peer, host_media, payload, 8, state, false,
          media_qit_recovery, 0, 0, true, mask) == ROB_OK);
    timeout_during_send = true;
    drain_one();
    CHECK(!timeout_during_send && peer.halow_pending_presentations == 0);
    CHECK(allocation_count == free_count);

    /* Each selected medium must own a distinct payload and result state. */
    host_media |= robusto_mt_mock;
    peer.supported_media_types = host_media;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_add) == ROB_ERR_TIMEOUT);
    CHECK(send_queue.count == 2);
    media_queue_item_t *first = STAILQ_FIRST(&pending);
    media_queue_item_t *second = STAILQ_NEXT(first, items);
    CHECK(first->state != second->state && first->data != second->data);
    drain_one();
    drain_one();
#else
    host_media = robusto_mt_mock;
    peer.supported_media_types = host_media;
    CHECK(robusto_send_presentation(&peer, host_media, false, presentation_add) == ROB_ERR_TIMEOUT);
    CHECK(send_queue.count == 1);
    drain_one();
#endif
    CHECK(allocation_count == free_count);
    /* Clean up leaked states when demonstrating a failing ownership regression. */
    for (unsigned i = 0; i < allocation_count; ++i) {
        if (allocations[i].live) free(allocations[i].pointer);
    }
    track_allocations = false;
}

static void test_large_admission(void)
{
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    robusto_peer_t peer;
    reset_peer(&peer, 12);
    uint8_t data[55002] = {0};
    queue_state state = {0};
    CHECK(send_queue.count == 0);
    robusto_send_admission_t admission = {0};
    send_queue.blocked = false;
    CHECK(robusto_prepare_send(NULL, robusto_mt_halow, sizeof(data), &admission) == ROB_ERR_INVALID_ARG);
    CHECK(robusto_prepare_send(&peer, robusto_mt_halow, sizeof(data), &admission) == ROB_OK);
    CHECK(send_message_raw_admitted(&admission, NULL, NULL, true) == ROB_ERR_INVALID_ARG);
    CHECK(!peer.halow_large_send_pending && send_queue.count == 0);
    CHECK(robusto_prepare_send(&peer, robusto_mt_halow, sizeof(data), &admission) == ROB_OK);
    robusto_cancel_send(&admission);
    robusto_cancel_send(&admission);
    CHECK(!peer.halow_large_send_pending && send_queue.count == 0);
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), &state, true) == ROB_OK);
    CHECK(peer.halow_large_send_pending && send_queue.count == 1);
    unsigned count = send_queue.count;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_ERR_QUEUE_FULL);
    CHECK(send_queue.count == count);
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, 8, NULL, false) == ROB_OK);
    CHECK(send_queue.count == count + 1);
    /* Dequeued/running work retains admission until completion, independent of queue depth. */
    media_queue_item_t *item = STAILQ_FIRST(&pending);
    STAILQ_REMOVE_HEAD(&pending, items);
    --send_queue.count;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_ERR_QUEUE_FULL);
    item->on_release_cb(item->peer, item->data_length);
    robusto_free(item);
    item = STAILQ_FIRST(&pending);
    STAILQ_REMOVE_HEAD(&pending, items);
    --send_queue.count;
    robusto_free(item);

    fail_after = 0;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_ERR_OUT_OF_MEMORY);
    fail_after = -1;
    CHECK(!peer.halow_large_send_pending);
    fail_queue_lock_once = true;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_ERR_MUTEX);
    CHECK(!peer.halow_large_send_pending && send_queue.count == 0);
    send_queue.count = send_queue.normal_max_count;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_ERR_QUEUE_FULL);
    CHECK(!peer.halow_large_send_pending);
    send_queue.count = 0;
    CHECK(send_message_raw(&peer, robusto_mt_halow, data, sizeof(data), NULL, true) == ROB_OK);
    item = STAILQ_FIRST(&pending);
    STAILQ_REMOVE_HEAD(&pending, items);
    --send_queue.count;
    item->on_release_cb(item->peer, item->data_length);
    robusto_free(item);
#endif
}

#ifdef PRESENTATION_CONTRACT_ALL_MEDIA
static rob_ret_val_t enqueue_other_media(robusto_peer_t *peer, e_media_type medium, uint32_t length)
{
    uint8_t *data = robusto_malloc(length);
    CHECK(data != NULL);
    rob_ret_val_t result = send_message_raw(peer, medium, data, length, NULL, false);
    if (result != ROB_OK) robusto_free(data);
    return result;
}

static void test_other_media_admission(void)
{
    static const e_media_type media[] = {
        robusto_mt_ble, robusto_mt_espnow, robusto_mt_lora,
        robusto_mt_i2c, robusto_mt_canbus, robusto_mt_mock
    };
    for (unsigned medium = 0; medium < sizeof(media) / sizeof(media[0]); ++medium) {
        robusto_peer_t peer;
        reset_peer(&peer, 13);
        unsigned outstanding = allocation_count - free_count;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
        peer.halow_large_send_pending = true;
        unsigned begins = large_begin_calls, ends = large_end_calls;
#endif
        CHECK(send_queue.count == 0);
        CHECK(enqueue_other_media(&peer, media[medium], 55002) == ROB_OK);
        CHECK(enqueue_other_media(&peer, media[medium], 55002) == ROB_OK);
        CHECK(enqueue_other_media(&peer, media[medium], 8) == ROB_OK);
        CHECK(send_queue.count == send_queue.normal_max_count);
        CHECK(enqueue_other_media(&peer, media[medium], 55002) == ROB_ERR_QUEUE_FULL);
        while (send_queue.count != 0) {
            media_queue_item_t *item = STAILQ_FIRST(&pending);
            STAILQ_REMOVE_HEAD(&pending, items);
            --send_queue.count;
            send_work_item(item, &dummy_media, media[medium], local_send, NULL, &send_queue);
        }
        CHECK(allocation_count - free_count == outstanding);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
        CHECK(peer.halow_large_send_pending);
        CHECK(large_begin_calls == begins && large_end_calls == ends);
#endif
        printf("Shared admission media=%u: multiple large sends, small sends, queue limit and completion passed\n",
               (unsigned)media[medium]);
    }
}
#endif

static void test_generic_admission(void)
{
    robusto_peer_t peer;
    reset_peer(&peer, 15);
    robusto_send_admission_t admission = {0}, other = {0};
    queue_state state = {0};
    generic_policy = track_allocations = true;
    send_queue.blocked = false;
    unsigned baseline = allocation_count - free_count;
    CHECK(robusto_prepare_send(NULL, robusto_mt_mock, 128, &admission) == ROB_ERR_INVALID_ARG);
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 0, &admission) == ROB_ERR_INVALID_ARG);
    CHECK(robusto_prepare_send(&peer, robusto_mt_none, 128, &admission) == ROB_FAIL);
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, NULL) == ROB_ERR_INVALID_ARG);
    CHECK(send_message_raw_admitted(NULL, NULL, &state, false) == ROB_ERR_INVALID_ARG);
    CHECK(send_message_raw_admitted(&admission, NULL, &state, false) == ROB_ERR_INVALID_ARG);
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_OK);
    CHECK(generic_busy && generic_preallocations == 1);
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_ERR_INVALID_ARG);
    unsigned allocated = allocation_count;
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &other) == ROB_ERR_QUEUE_FULL);
    CHECK(!other.active && allocation_count == allocated);
    CHECK(send_message_raw_admitted(&admission, NULL, &state, false) == ROB_ERR_INVALID_ARG);
    CHECK(!generic_busy && !admission.active && generic_releases == 1);
    robusto_cancel_send(&admission);
    CHECK(generic_releases == 1);

    for (unsigned failure = 0; failure < 4; ++failure) {
        CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_OK);
        uint8_t *data = robusto_malloc(128);
        if (failure == 0) fail_after = 0;
        if (failure == 1) fail_queue_lock_once = true;
        if (failure == 2) send_queue.count = send_queue.normal_max_count;
        if (failure == 3) send_queue.shutdown = true;
        static const rob_ret_val_t expected[] = {
            ROB_ERR_OUT_OF_MEMORY, ROB_ERR_MUTEX, ROB_ERR_QUEUE_FULL, ROB_ERR_MUTEX
        };
        CHECK(send_message_raw_admitted(&admission, data, &state, false) == expected[failure]);
        fail_after = -1;
        send_queue.count = 0;
        send_queue.shutdown = false;
        robusto_free(data);
        CHECK(!generic_busy && !admission.active);
        CHECK(allocation_count - free_count == baseline);
    }
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_OK);
    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 8, &other) == ROB_OK);
    CHECK(send_message_raw_admitted(&other, robusto_malloc(8), NULL, false) == ROB_OK);
    drain_one();
    CHECK(generic_busy);
    robusto_cancel_send(&admission);
    CHECK(!generic_busy);

    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_OK);
    unsigned releases = generic_releases;
    drain_on_unlock = true;
    CHECK(send_message_raw_admitted(&admission, robusto_malloc(128), NULL, false) == ROB_OK);
    CHECK(!admission.active && !generic_busy && send_queue.count == 0);
    CHECK(generic_releases == releases + 1);
    robusto_cancel_send(&admission);
    CHECK(generic_releases == releases + 1);
    CHECK(send_message_raw_admitted(&admission, NULL, NULL, false) == ROB_ERR_INVALID_ARG);

    releases = generic_releases;
    CHECK(send_message_raw(&peer, robusto_mt_mock, robusto_malloc(128), 128, NULL, false) == ROB_OK);
    CHECK(generic_busy && generic_raw_admissions == 1);
    drain_one();
    CHECK(!generic_busy && generic_releases == releases + 1);

    CHECK(robusto_prepare_send(&peer, robusto_mt_mock, 128, &admission) == ROB_OK);
    CHECK(send_message_raw_admitted(&admission, robusto_malloc(128), NULL, true) == ROB_OK);
    robusto_media_types previous_host_media = host_media;
    host_media = robusto_mt_mock;
    use_pubsub_media = true;
    local_send_result = ROB_ERR_SEND_FAIL;
    unsigned calls = local_send_calls;
    releases = generic_releases;
    drain_one();
    CHECK(local_send_calls == calls + 3 && !generic_busy && generic_releases == releases + 1);
    host_media = previous_host_media;
    local_send_result = ROB_OK;
    pubsub_media = robusto_mt_mock;
    uint32_t topic = robusto_pubsub_server_subscribe(&peer, NULL, "fixture.images");
    uint8_t payload[128] = {0};
    generic_busy = true;
    allocated = allocation_count;
    CHECK(robusto_pubsub_server_publish(topic, payload, sizeof(payload)) == ROB_ERR_SEND_SOME_FAIL);
    CHECK(allocation_count == allocated && generic_busy && send_queue.count == 0);
    generic_busy = false;
    for (int failure = 0; failure < 2; ++failure) {
        fail_after = failure;
        CHECK(robusto_pubsub_server_publish(topic, payload, sizeof(payload)) == ROB_ERR_SEND_SOME_FAIL);
        fail_after = -1;
        CHECK(!generic_busy && send_queue.count == 0);
    }
    CHECK(robusto_pubsub_server_publish(topic, payload, sizeof(payload)) == ROB_OK);
    CHECK(generic_busy && send_queue.count == 1);
    drain_one();
    CHECK(!generic_busy);
    CHECK(robusto_pubsub_server_unsubscribe_peer_from_all(&peer) == 1);
    use_pubsub_media = generic_policy = false;
    CHECK(allocation_count - free_count == baseline);
    track_allocations = false;
    puts("Generic admission: preallocation rejection, rollback, small traffic, immediate completion, implicit admission and retry lifetime passed");
}

static void test_pubsub_admission_before_allocation(void)
{
    robusto_peer_t peer;
    reset_peer(&peer, 14);
    peer.state = PEER_KNOWN_INSECURE;
    dummy_media.state = media_state_working;
    use_pubsub_media = use_psram = true;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    pubsub_media = robusto_mt_halow;
#else
    pubsub_media = robusto_mt_mock;
#endif
    CHECK(robusto_pubsub_server_init("pubsub-admission") == ROB_OK);
    uint32_t image_topic = robusto_pubsub_server_subscribe(&peer, NULL, "fixture.images");
    uint32_t detection_topic = robusto_pubsub_server_subscribe(&peer, NULL, "fixture.detections");
    uint8_t image[54990] = {0};
    uint8_t detection[284] = {0};
    unsigned baseline = allocation_count - free_count;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    CHECK(robusto_halow_begin_large_send(&peer) == ROB_OK);
    unsigned allocated = allocation_count, external = psram_allocations;
    CHECK(robusto_pubsub_server_publish(image_topic, image, sizeof(image)) == ROB_ERR_SEND_SOME_FAIL);
    CHECK(allocation_count == allocated && psram_allocations == external && send_queue.count == 0);
    robusto_halow_end_large_send(&peer);
    for (int fail = 0; fail < 2; ++fail) {
        fail_after = fail;
        CHECK(robusto_pubsub_server_publish(image_topic, image, sizeof(image)) == ROB_ERR_SEND_SOME_FAIL);
        fail_after = -1;
        CHECK(!peer.halow_large_send_pending && send_queue.count == 0);
        CHECK(allocation_count - free_count == baseline);
    }
    send_queue.count = send_queue.normal_max_count;
    allocated = allocation_count;
    external = psram_allocations;
    CHECK(robusto_pubsub_server_publish(image_topic, image, sizeof(image)) == ROB_ERR_SEND_SOME_FAIL);
    CHECK(allocation_count == allocated && psram_allocations == external && !peer.halow_large_send_pending);
    send_queue.count = 0;
#endif
    unsigned external_before = psram_allocations;
    CHECK(robusto_pubsub_server_publish(image_topic, image, sizeof(image)) == ROB_OK);
    CHECK(psram_allocations == external_before + 1 && send_queue.count == 1);
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    CHECK(peer.halow_large_send_pending);
    allocated = allocation_count;
    CHECK(robusto_pubsub_server_publish(image_topic, image, sizeof(image)) == ROB_ERR_SEND_SOME_FAIL);
    CHECK(allocation_count == allocated && send_queue.count == 1);
#endif
    CHECK(robusto_pubsub_server_publish(detection_topic, detection, sizeof(detection)) == ROB_OK);
    CHECK(send_queue.count == 2);
    while (send_queue.count != 0) drain_one();
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    CHECK(!peer.halow_large_send_pending);
#endif
    CHECK(allocation_count - free_count == baseline);
    CHECK(robusto_pubsub_server_unsubscribe_peer_from_all(&peer) == 2);
    use_pubsub_media = use_psram = false;
    puts("Production PubSub: reservation precedes image allocation, rollback, PSRAM selection and detection admission passed");
}

int main(void)
{
    CHECK(init_crc32() == 0);
    memset(&host_peer, 0, sizeof(host_peer));
    memcpy(host_peer.base_mac_address, HOST_MAC, 6);
    strcpy(host_peer.name, "host");
    robusto_presentation_init("presentation-contract");

    test_make_legacy_v0_byte_for_byte();
    test_make_v1_for_halow_peer();
    test_parse_v0_legacy();
    test_parse_v1_high_bit();
    test_halow_mapping_preserved();
    test_malformed_rejected();
    CHECK(send_calls == 0);
    test_pending_presentations();
    test_large_admission();
#ifdef PRESENTATION_CONTRACT_ALL_MEDIA
    test_other_media_admission();
#endif
    test_pubsub_admission_before_allocation();
    test_generic_admission();
    track_allocations = false;
    cleanup_crc32();

    if (failures)
    {
        printf("presentation contract FAILED (%d)\n", failures);
        return 1;
    }
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    printf("presentation contract passed (HaLow build)\n");
#else
    printf("presentation contract passed (non-HaLow build)\n");
#endif
    return 0;
}
