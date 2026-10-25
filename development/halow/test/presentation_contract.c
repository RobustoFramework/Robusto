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

/* Stubs for the framework dependencies of robusto_presentation.c */
robusto_peer_t *get_host_peer() { return &host_peer; }
robusto_media_types get_host_supported_media_types() { return host_media; }
uint32_t calc_relation_id(uint8_t *mac_1, uint8_t *mac_2) { (void)mac_1; (void)mac_2; return RELATION_ID; }
void *robusto_malloc(size_t size) { return malloc(size); }
char *media_type_to_str(robusto_media_types media_type) { (void)media_type; return "mt"; }
void rob_log_bit_mesh(rob_log_level_t level, const char *tag, const uint8_t *data, int data_len) { (void)level; (void)tag; (void)data; (void)data_len; }
void rob_log_write(rob_log_level_t level, const char *tag, const char *format, ...) { (void)level; (void)tag; (void)format; }
robusto_media_t *get_media_info(robusto_peer_t *peer, e_media_type media_type) { (void)peer; (void)media_type; return &dummy_media; }
void set_state(robusto_peer_t *peer, robusto_media_t *info, e_media_type media_type, e_media_state media_state, e_media_problem problem) { (void)peer; (void)info; (void)media_type; (void)media_state; (void)problem; }
void init_supported_media_types(robusto_peer_t *peer) { (void)peer; init_media_calls++; }
void log_peer_info(char *_log_prefix, robusto_peer_t *peer) { (void)_log_prefix; (void)peer; }
bool notify_on_new_peer(robusto_peer_t *peer) { (void)peer; return false; }
robusto_peer_t *robusto_peers_find_duplicate_by_base_mac_address(robusto_peer_t *check_peer) { (void)check_peer; return existing_peer_result; }
void robusto_free(void *ptr) { free(ptr); }
unsigned long r_micros() { return 0; }
static e_msg_type_t last_envelope_type = MSG_HEARTBEAT;
/* Envelope building is a separate production unit; capture the presentation payload verbatim. */
int robusto_make_multi_message_internal(e_msg_type_t message_type, uint16_t service_id, uint16_t conversation_id,
                                        uint8_t *strings_data, uint32_t strings_length,
                                        uint8_t *binary_data, uint32_t binary_length, uint8_t **dest_message)
{
    (void)service_id; (void)conversation_id; (void)strings_data; (void)strings_length;
    last_envelope_type = message_type;
    *dest_message = malloc(binary_length);
    memcpy(*dest_message, binary_data, binary_length);
    return (int)binary_length;
}
int robusto_peers_delete_peer(uint16_t peer_handle) { deleted_handle = peer_handle; return 0; }
rob_ret_val_t robusto_create_task(TaskFunction_t task_function, void *parameter, char *task_name, rob_task_handle_t **handle, int affinity) { (void)task_function; (void)parameter; (void)task_name; (void)handle; (void)affinity; return ROB_OK; }
rob_ret_val_t robusto_delete_current_task() { return ROB_OK; }
bool robusto_waitfor_byte(uint8_t *byte_ptr, uint8_t value, uint32_t timeout_ms) { (void)byte_ptr; (void)value; (void)timeout_ms; return false; }
void r_delay(unsigned long milliseconds) { (void)milliseconds; }
bool robusto_waitfor_queue_state(queue_state *state, uint32_t timeout_ms, rob_ret_val_t *return_value) { (void)state; (void)timeout_ms; (void)return_value; return false; }
queue_state *robusto_free_queue_state(queue_state *state) { free(state); return NULL; }
rob_ret_val_t send_message_raw_internal(robusto_peer_t *peer, e_media_type media_type, uint8_t *data, uint32_t data_length, queue_state *state, bool receipt, e_media_queue_item_type queue_item_type, uint8_t depth, robusto_media_types exclude_media_types, bool important)
{ (void)peer; (void)media_type; (void)data; (void)data_length; (void)state; (void)receipt; (void)queue_item_type; (void)depth; (void)exclude_media_types; (void)important; send_calls++; return ROB_FAIL; }
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
