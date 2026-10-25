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
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
static bool last_relation_halow_valid = false;
static uint8_t last_relation_halow_mac[6];
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
rob_ret_val_t robusto_mutex_take(mutex_ref_t mutex, int timeout) { (void)mutex; (void)timeout; return ROB_OK; }
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
queue_context_t *mock_get_queue_context(void)
{
    ++send_calls;
    return queue_sends ? &send_queue : NULL;
}
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
queue_context_t *halow_get_queue_context(void) { return mock_get_queue_context(); }
#endif
rob_ret_val_t set_suitable_media(robusto_peer_t *peer, uint16_t length, robusto_media_types exclude, e_media_type *result)
{
    (void)peer; (void)length; (void)exclude; (void)result;
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
    CHECK(!receipt);
    if (timeout_during_send) {
        timeout_during_send = false;
        CHECK((*active_state)[0] == QUEUE_STATE_RUNNING);
        unsigned freed = free_count;
        robusto_free_queue_state(active_state);
        CHECK((*active_state)[0] == QUEUE_STATE_DROPPED && free_count == freed);
    }
    return ROB_OK;
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
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
                  , const rob_mac_address *halow_mac_address, bool halow_address_valid
#endif
)
{
    (void)mac_address; (void)relation_id_incoming; (void)relation_id_outgoing;
    add_relation_calls++;
    last_relation_media = supported_media_types;
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
    expected[pos++] = 0;
    expected[pos++] = 0;
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

int main(void)
{
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
