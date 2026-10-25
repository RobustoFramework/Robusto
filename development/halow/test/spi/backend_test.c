#include "backend_test_stubs.h"
#include "halow_spi.c"
#include <stdio.h>
#include <stdarg.h>

test_semaphore_t test_semaphores[2];
unsigned int test_semaphore_count, test_uart_calls, test_init_calls;
unsigned int test_cleanup_failures;
unsigned int test_lock_failures;
TickType_t test_ticks;
bool test_quiesce_timeout, test_fail_lock_after_exit;
bool test_invalid_gpio;
TaskHandle_t test_current_task = (void *)(uintptr_t)1;
spi_dev_t test_spi;
test_gpio_t test_gpio;

static robusto_halow_driver_t test_driver;
static bool test_started, test_link_ready, test_reset_fails, test_no_credits;
static bool test_robusto_initialized, test_reset_clears_failure;
static unsigned int test_starts, test_stops, test_resets, test_health_calls;
static unsigned int test_link_failures, test_receiver_exits;
static unsigned int test_init_failures;
static unsigned int test_commands[32], test_command_count;
static int test_pending_command, test_reject_command, test_drop_command;
static bool test_bad_mode, test_bad_probe, test_drop_probe, test_driving_receiver;
static bool test_change_role_after_up;
static unsigned int test_probe_count, test_link_ups, test_delay_count, test_delay_limit;
static int test_pending_event;
static int test_mismatch_command, test_truncated_command, test_submit_failure;
static bool test_terminated_ssid;
static uint8_t test_event_payload[64];
static unsigned int test_event_length = 6;
static unsigned int test_connection_start_logs, test_connection_failure_logs;
static char test_last_connection_log[256];
static char test_last_log[512];
static unsigned int test_radio_enable_logs;

void test_log(const char *tag, const char *format, ...)
{
    (void)tag;
#if defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE)
    assert(strcmp(format, "Stored RF/security settings unchanged") != 0);
#endif
    va_list all_arguments;
    va_start(all_arguments, format);
    int log_size = vsnprintf(test_last_log, sizeof(test_last_log), format, all_arguments);
    va_end(all_arguments);
    assert(log_size >= 0 && (size_t)log_size < sizeof(test_last_log));
#ifdef NETWORK_WPA_PSK
    if (strlen(network_psk) == 64) assert(strstr(test_last_log, network_psk) == NULL);
#endif
    if (strstr(format, "Radio enable command 65 acknowledged") != NULL)
        ++test_radio_enable_logs;
    if (strcmp(format, "Connection attempt started (event 11)") == 0) {
        ++test_connection_start_logs;
    } else if (strstr(format, "Connection failed (event 22)") != NULL) {
        ++test_connection_failure_logs;
        va_list arguments;
        va_start(arguments, format);
        int size = vsnprintf(test_last_connection_log, sizeof(test_last_connection_log),
                             format, arguments);
        va_end(arguments);
        assert(size > 0 && (size_t)size < sizeof(test_last_connection_log));
    }
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
#define TEST_STATUS_COMMAND HGIC_CMD_GET_MODE
#else
#define TEST_STATUS_COMMAND HGIC_CMD_GET_CONN_STATE
#endif

void test_receive_delay(void)
{
    if (!test_driving_receiver) return;
    ++test_delay_count;
    assert(test_delay_count < 700);
    if (test_change_role_after_up && test_link_ready) {
        test_pending_command = HGIC_CMD_GET_MODE;
        test_bad_mode = true;
    }
    if (test_delay_limit && test_delay_count >= test_delay_limit) stopping = true;
}

void test_finish_receiver(void)
{
    assert(stopping && rx_task != NULL);
    ++test_receiver_exits;
    receive_task(NULL);
}

rob_ret_val_t robusto_halow_register_driver(const robusto_halow_driver_t *binding)
{ test_driver = *binding; return ROB_OK; }

rob_ret_val_t robusto_halow_start(void)
{
    assert(!test_started);
    ++test_starts;
    rob_ret_val_t result = test_driver.start(test_driver.context);
    test_started = result == ROB_OK;
    return result;
}

rob_ret_val_t robusto_halow_stop(void)
{
    assert(test_started);
    ++test_stops;
    test_link_ready = false;
    rob_ret_val_t result = test_driver.stop(test_driver.context);
    if (result == ROB_OK) test_started = false;
    return result;
}

rob_ret_val_t robusto_halow_set_link_ready(bool ready)
{
    if (!test_robusto_initialized) return ROB_ERR_NOT_READY;
    if (test_link_failures != 0) { --test_link_failures; return ROB_FAIL; }
    test_link_ready = ready;
    if (ready) ++test_link_ups;
    return ROB_OK;
}

rob_ret_val_t robusto_halow_receive(const uint8_t source[6], const uint8_t *frame,
                                   uint32_t length, const int16_t *rssi)
{ (void)source; (void)frame; (void)length; (void)rssi; assert(false); return ROB_FAIL; }

int hgic_sdspi_init(void *priv)
{
    (void)priv;
    ++test_init_calls;
    test_pending_command = 0;
    if (test_init_failures != 0) { --test_init_failures; return -1; }
    return 0;
}

int hgic_sdspi_get_devid(void *priv, unsigned short *vendor, unsigned short *product)
{ (void)priv; *vendor = 1; *product = 2; return 0; }

int hgic_sdspi_detect_alive(void *priv)
{ (void)priv; ++test_health_calls; return 0; }

int hgic_sdspi_write(void *priv, unsigned char *data, unsigned int length)
{
    (void)priv;
    if (data[2] == HGIC_HDR_TYPE_SOFTFC) {
        test_pending_command = -1;
        return length;
    }
    if (data[2] == HGIC_HDR_TYPE_TEST2) {
        assert(length == sizeof(struct hgic_hdr) + 4);
        assert(memcmp(data + sizeof(struct hgic_hdr), ROBUSTO_HALOW_SPI_AP_PROBE, 4) == 0);
        ++test_probe_count;
        test_pending_command = -2;
        return length;
    }
    if (data[2] == HGIC_HDR_TYPE_FRM2) {
        assert(length <= 1664);
        return length;
    }
    unsigned int command_id = data[8];
    assert(test_command_count < sizeof(test_commands) / sizeof(test_commands[0]));
    test_commands[test_command_count++] = command_id;
    if ((int)command_id == test_submit_failure) return -1;
    if (command_id == HGIC_CMD_RADIO_ONOFF) {
        assert(length == CMD_HDR_LEN + 1 && data[CMD_HDR_LEN] == 1);
        assert(!test_link_ready && !ap_interface_ready);
    }
#if defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE)
    assert(command_id != HGIC_CMD_GET_WPA_PSK && command_id != HGIC_CMD_SET_CHANNEL &&
           command_id != HGIC_CMD_SET_COUNTERY && command_id != HGIC_CMD_PAIRING);
    if (command_id == HGIC_CMD_SET_SSID) {
        assert(length == CMD_HDR_LEN + strlen(network_ssid));
        assert(memcmp(data + CMD_HDR_LEN, network_ssid, strlen(network_ssid)) == 0);
    }
#ifdef NETWORK_WPA_PSK
    if (command_id == HGIC_CMD_SET_WPA_PSK) {
        assert(length == CMD_HDR_LEN + 64);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
        assert(memcmp(data + CMD_HDR_LEN, CONFIG_ROBUSTO_HALOW_SPI_STA_PSK, 64) == 0);
#else
        assert(memcmp(data + CMD_HDR_LEN, CONFIG_ROBUSTO_HALOW_SPI_AP_PSK, 64) == 0);
#endif
        assert(!trace_control_submission);
    }
#else
    assert(command_id != HGIC_CMD_SET_WPA_PSK);
#endif
    if (command_id == HGIC_CMD_SET_KEY_MGMT) {
#ifdef NETWORK_WPA_PSK
        assert(length == CMD_HDR_LEN + 7);
        assert(memcmp(data + CMD_HDR_LEN, "WPA-PSK", 7) == 0);
#else
        assert(length == CMD_HDR_LEN + 4);
        assert(memcmp(data + CMD_HDR_LEN, "NONE", 4) == 0);
#endif
    }
    if (command_id == HGIC_CMD_SET_BSS_BW) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
        assert(length == CMD_HDR_LEN + 1 && data[CMD_HDR_LEN] == 1);
#else
        assert(length == CMD_HDR_LEN + 1 && data[CMD_HDR_LEN] == 2);
#endif
    }
    if (command_id == HGIC_CMD_SET_CHAN_LIST) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
        assert(length == CMD_HDR_LEN + 4);
        assert(get_unaligned_le16(data + CMD_HDR_LEN) == 1);
        assert(get_unaligned_le16(data + CMD_HDR_LEN + 2) == 8655);
#else
        assert(length == CMD_HDR_LEN + 6);
        assert(get_unaligned_le16(data + CMD_HDR_LEN) == 2);
        assert(get_unaligned_le16(data + CMD_HDR_LEN + 2) == 9080);
        assert(get_unaligned_le16(data + CMD_HDR_LEN + 4) == 9160);
#endif
    }
#endif
    if (command_id == HGIC_CMD_SET_WORK_MODE) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
        assert(length == CMD_HDR_LEN + 2 && memcmp(data + CMD_HDR_LEN, "ap", 2) == 0);
#else
        assert(length == CMD_HDR_LEN + 3 && memcmp(data + CMD_HDR_LEN, "sta", 3) == 0);
#endif
    }
    if (command_id == HGIC_CMD_RESET) {
        assert(stopping && rx_task == NULL && !test_link_ready);
        ++test_resets;
        if (!test_reset_fails && test_reset_clears_failure) {
            test_drop_command = test_reject_command = 0;
        }
        return test_reset_fails ? -1 : (int)length;
    }
    test_pending_command = command_id;
    return length;
}

int hgic_sdspi_read(void *priv, unsigned char *data, unsigned int capacity, int flags)
{
    (void)priv; (void)flags;
    if (test_pending_event != 0) {
        memset(data, 0, capacity);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
        data[2] = HGIC_HDR_TYPE_EVENT;
        data[8] = test_pending_event;
        assert(test_event_length <= sizeof(test_event_payload));
        put_unaligned_le16(CMD_HDR_LEN + test_event_length, data + 4);
        memcpy(data + CMD_HDR_LEN, test_event_payload, test_event_length);
        test_pending_event = 0;
        return (CMD_HDR_LEN + test_event_length + 3) & ~3U;
    }
    if (test_pending_command == 0) return 0;
    if (test_pending_command == test_drop_command) return 0;
    memset(data, 0, capacity);
    put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
    if (test_pending_command == -2) {
        if (test_drop_probe) return 0;
        data[2] = HGIC_HDR_TYPE_TEST2;
        put_unaligned_le16(sizeof(struct hgic_hdr) + 4, data + 4);
        memcpy(data + sizeof(struct hgic_hdr), test_bad_probe ? "FAIL" :
               ROBUSTO_HALOW_SPI_AP_PROBE, 4);
        test_pending_command = 0;
        return sizeof(struct hgic_hdr) + 4;
    }
    if (test_pending_command == -1) {
        if (test_no_credits) return 0;
        data[2] = HGIC_HDR_TYPE_SOFTFC;
        put_unaligned_le16(sizeof(struct hgic_hdr), data + 4);
        put_unaligned_le16(2, data + 6);
        test_pending_command = 0;
        return sizeof(struct hgic_hdr);
    }
    unsigned int id = test_pending_command;
    data[2] = HGIC_HDR_TYPE_CMD;
    data[8] = id;
    unsigned int length = CMD_HDR_LEN;
    if ((int)id == test_reject_command) {
        put_unaligned_le16((uint16_t)-1, data + 10);
    } else if (id == HGIC_CMD_GET_FW_INFO) {
        length += sizeof(hgic.fwinfo);
    } else if (id == HGIC_CMD_GET_MODE) {
        const char *mode = test_bad_mode ? "sta" : "ap";
        unsigned int size = test_bad_mode ? 3 : 2;
        put_unaligned_le16(size, data + 10);
        memcpy(data + CMD_HDR_LEN, mode, size);
        length += size;
    }
#if defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE)
    else if (id == HGIC_CMD_GET_SSID || id == HGIC_CMD_GET_KEY_MGMT) {
        const char *text = network_ssid;
        if (id == HGIC_CMD_GET_KEY_MGMT) {
#ifdef NETWORK_WPA_PSK
            text = "WPA-PSK";
#else
            text = "NONE";
#endif
        }
        unsigned int size = strlen(text) + (test_terminated_ssid ? 1 : 0);
        memcpy(data + CMD_HDR_LEN, text, size);
        put_unaligned_le16(size, data + 10);
        length += size;
    } else if (id == HGIC_CMD_GET_BSS_BW) {
        put_unaligned_le16(NETWORK_BANDWIDTH, data + 10);
    } else if (id == HGIC_CMD_GET_CHAN_LIST) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
        put_unaligned_le16(2, data + 10);
        put_unaligned_le16(8655, data + CMD_HDR_LEN);
        length += 2;
#else
        put_unaligned_le16(4, data + 10);
        put_unaligned_le16(9080, data + CMD_HDR_LEN);
        put_unaligned_le16(9160, data + CMD_HDR_LEN + 2);
        length += 4;
#endif
    }
#endif
    if ((int)id == test_mismatch_command) {
        if (length > CMD_HDR_LEN) data[CMD_HDR_LEN] ^= 1;
        else put_unaligned_le16(3, data + 10);
    }
    if ((int)id == test_truncated_command) length = CMD_HDR_LEN;
    put_unaligned_le16(length, data + 4);
    test_pending_command = 0;
    return length;
}

unsigned char crc7_be(unsigned char crc, const unsigned char *buffer, unsigned int length)
{ (void)buffer; (void)length; return crc; }

static void clear_fixture(void)
{
    assert(!running && rx_task == NULL && !hardware_allocated());
    test_started = test_link_ready = test_reset_fails = test_no_credits = false;
    test_robusto_initialized = true;
    test_reset_clears_failure = false;
    test_invalid_gpio = false;
    test_quiesce_timeout = false;
    test_fail_lock_after_exit = false;
    test_lock_failures = test_link_failures = test_receiver_exits = 0;
    test_cleanup_failures = test_starts = test_stops = test_resets = 0;
    test_init_calls = test_command_count = test_uart_calls = test_health_calls = 0;
    test_init_failures = 0;
    test_pending_command = test_reject_command = test_drop_command = 0;
    test_bad_mode = test_bad_probe = test_drop_probe = test_driving_receiver = false;
    test_change_role_after_up = false;
    test_pending_event = 0;
    memset(test_event_payload, 0, sizeof(test_event_payload));
    test_event_length = 6;
    test_connection_start_logs = test_connection_failure_logs = 0;
    test_last_connection_log[0] = '\0';
    test_last_log[0] = '\0';
    test_radio_enable_logs = 0;
    bitrate_logs = signal_logs = 0;
    test_terminated_ssid = false;
    test_mismatch_command = test_truncated_command = test_submit_failure = 0;
    test_probe_count = test_link_ups = test_delay_count = test_delay_limit = 0;
    test_ticks = 0;
}

static void start_fixture(void)
{
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(test_init_calls == 1 && test_command_count == 4);
    assert(test_commands[0] == 43 && test_commands[1] == 41 &&
           test_commands[2] == 65 && test_commands[3] == TEST_STATUS_COMMAND);
    assert(test_radio_enable_logs == 1);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    assert(ap_interface_ready && test_probe_count == 1);
#else
    assert(!ap_interface_ready && test_probe_count == 0);
#endif
    assert(test_resets == 0 && !hgic.status.conn_state);
    assert(test_uart_calls == 0 && tx_descriptor == NULL);
    assert(!test_link_ready && running && rx_task != NULL);
}

static void assert_stopped(void)
{
    assert(!test_started && !running && rx_task == NULL && device == NULL);
    assert(!bus_initialized && !irq_initialized && !cs_initialized && !test_link_ready);
    assert(!ap_interface_ready);
}

static void test_connection_event_diagnostics(void)
{
    clear_fixture();
    robusto_halow_spi_packet_t packet;
    hgic.status.conn_state = 1;
    test_pending_event = HGIC_EVENT_CONECT_START;
    test_event_length = 0;
    assert(read_packet(&packet) == 1);
    assert(test_connection_start_logs == 1 && test_connection_failure_logs == 0);
    test_pending_event = HGIC_EVENT_CONNECT_FAIL;
    test_event_length = 1;
    test_event_payload[0] = 0xa5;
    assert(read_packet(&packet) == 1);
    assert(test_connection_failure_logs == 1);
    assert(strstr(test_last_connection_log, "detail=0xa5 (165), payload=1 bytes") != NULL);
    assert(strstr(test_last_connection_log, "meaning unverified") != NULL);
    assert(packet.event_length == 1 && packet.frame == NULL && packet.length == 0);
    assert(hgic.status.conn_state == 1 && !faulted);
    test_pending_event = HGIC_EVENT_CONNECT_FAIL;
    test_event_length = 0;
    assert(read_packet(&packet) == 0);
    assert(test_connection_failure_logs == 1);
    test_pending_event = HGIC_EVENT_CONNECT_FAIL;
    test_event_length = 3;
    test_event_payload[0] = 0xff;
    test_event_payload[1] = 0xde;
    test_event_payload[2] = 0xad;
    assert(read_packet(&packet) == 1);
    assert(test_connection_failure_logs == 2);
    assert(strstr(test_last_connection_log, "detail=0xff (255), payload=3 bytes") != NULL);
    assert(strstr(test_last_connection_log, "dead") == NULL);
    assert(hgic.status.conn_state == 1 && !faulted && !test_link_ready);
    hgic.status.conn_state = 0;
    clear_fixture();
    puts("HaLow connection event diagnostics preserve bounded raw detail and leave association/lifecycle state unchanged.");
}

static void test_all_event_diagnostics(void)
{
    clear_fixture();
    robusto_halow_spi_packet_t packet;
    for (unsigned int id = 1; id <= 31; ++id) {
        test_pending_event = id;
        test_event_length = id == HGIC_EVENT_PAIR_DONE ? 12 : 4;
        if (id == HGIC_EVENT_CONECTED) test_event_length = 6;
        memset(test_event_payload, 0, sizeof(test_event_payload));
        if (id == HGIC_EVENT_EXCEPTION_INFO)
            put_unaligned_le32(HGIC_EXCEPTION_CPU_USED_OVERTOP, test_event_payload);
        assert(read_packet(&packet) == 1);
        assert(packet.event && packet.event_id == id && !packet.unsupported);
        assert(packet.frame == NULL && packet.length == 0);
        assert(!faulted && !test_link_ready);
        if (id != HGIC_EVENT_CONECT_START && id != HGIC_EVENT_CONNECT_FAIL)
            assert(strstr(test_last_log, robusto_halow_spi_event_name(id)) != NULL);
    }
    assert(hgic.paired_stas_cnt == 2);
    for (unsigned int i = 0; i < 10; ++i) {
        test_pending_event = HGIC_EVENT_SIGNAL;
        test_event_length = 2;
        assert(read_packet(&packet) == 1);
        test_pending_event = HGIC_EVENT_TX_BITRATE;
        test_event_length = 4;
        assert(read_packet(&packet) == 1);
    }
    assert(bitrate_logs == 5 && signal_logs == 5);
    const char *exception_names[] = {
        "", "CPU_USED_OVERTOP", "HEAP_USED_OVERTOP", "WIFI_BUFFER_USED_OVERTOP",
        "TX_BLOCKED", "TXDELAY_TOOLONG", "STRONG_BGRSSI", "TEMPERATURE_OVERTOP",
        "WRONG_PASSWORD", "unknown code=9"
    };
    for (unsigned int code = 1; code <= 9; ++code) {
        test_pending_event = HGIC_EVENT_EXCEPTION_INFO;
        test_event_length = 16;
        put_unaligned_le32(code, test_event_payload);
        put_unaligned_le32(100, test_event_payload + 4);
        put_unaligned_le32(200, test_event_payload + 8);
        put_unaligned_le32(300, test_event_payload + 12);
        assert(read_packet(&packet) == 1);
        assert(strstr(test_last_log, "EXCEPTION_INFO") != NULL);
        assert(strstr(test_last_log, exception_names[code]) != NULL);
        if (code == 2 || code == 3)
            assert(strstr(test_last_log, "total=100 used=200") != NULL);
        if (code == 5 || code == 6)
            assert(strstr(test_last_log, "max=100 avg=200 min=300") != NULL);
        if (code == 7) assert(strstr(test_last_log, "value=100") != NULL);
    }
    test_pending_event = HGIC_EVENT_EXCEPTION_INFO;
    test_event_length = 3;
    assert(read_packet(&packet) == 0);
    test_pending_event = 32;
    test_event_length = 0;
    assert(read_packet(&packet) == 0 && packet.unsupported);
    hgic.status.conn_state = 0;
    clear_fixture();
    puts("All vendor event IDs logged safely; exceptions, telemetry limits and unknown/malformed handling passed.");
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    test_connection_event_diagnostics();
    test_all_event_diagnostics();
#if defined(TEST_STA_INVALID_SSID) || defined(TEST_STA_INVALID_PSK) || defined(TEST_STA_INVALID_BANDWIDTH) || defined(TEST_STA_INVALID_FREQUENCIES)
    clear_fixture();
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_init_calls == 0 && test_command_count == 0);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_ARG);
    assert(test_init_calls == 0 && test_resets == 0);
    assert_stopped();
    puts("HaLow invalid STA SSID/PSK/bandwidth/frequencies rejected before transport or module reset.");
    return 0;
#endif
    clear_fixture();
    char saved = network_ssid[0];
    network_ssid[0] = '\0';
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_init_calls == 0 && test_command_count == 0);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_ARG);
    assert(test_init_calls == 0 && test_resets == 0);
    assert_stopped();
    network_ssid[0] = saved;

#ifdef NETWORK_WPA_PSK
    const size_t invalid_positions[] = {0, 63};
    for (size_t i = 0; i < sizeof(invalid_positions) / sizeof(invalid_positions[0]); ++i) {
        clear_fixture();
        size_t position = invalid_positions[i];
        char saved_psk = network_psk[position];
        network_psk[position] = '\0';
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_init_calls == 0 && test_command_count == 0);
        assert(reset_and_reinitialize() == ESP_ERR_INVALID_ARG);
        assert(test_init_calls == 0 && test_resets == 0);
        assert_stopped();
        network_psk[position] = 'g';
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_init_calls == 0 && test_command_count == 0);
        assert_stopped();
        network_psk[position] = saved_psk;
    }
#endif

    const unsigned int sequence[] = {HGIC_CMD_GET_FW_INFO, HGIC_CMD_SET_SSID,
#ifdef NETWORK_WPA_PSK
        HGIC_CMD_SET_WPA_PSK,
#endif
        HGIC_CMD_SET_KEY_MGMT, HGIC_CMD_SET_BSS_BW, HGIC_CMD_SET_CHAN_LIST,
        HGIC_CMD_SET_WORK_MODE, HGIC_CMD_RADIO_ONOFF, HGIC_CMD_GET_SSID,
        HGIC_CMD_GET_KEY_MGMT, HGIC_CMD_GET_BSS_BW, HGIC_CMD_GET_CHAN_LIST,
        HGIC_CMD_GET_CONN_STATE};
    size_t sequence_count = sizeof(sequence) / sizeof(sequence[0]);
    for (unsigned int terminated = 0; terminated < 2; ++terminated) {
        clear_fixture();
        test_terminated_ssid = terminated != 0;
        assert(robusto_halow_start() == ROB_OK);
        assert(test_command_count == sizeof(sequence) / sizeof(sequence[0]));
        assert(memcmp(test_commands, sequence, sizeof(sequence)) == 0);
        assert(!hgic.status.conn_state && !test_link_ready && test_probe_count == 0);
        test_command_count = 0;
        assert(reset_and_reinitialize() == ESP_OK);
        assert(test_command_count == sequence_count + 1 && test_commands[0] == HGIC_CMD_RESET);
        assert(memcmp(test_commands + 1, sequence, sizeof(sequence)) == 0);
        assert(robusto_halow_stop() == ROB_OK);
        assert_stopped();
    }
    saved = network_ssid[1];
    network_ssid[1] = '\0';
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    network_ssid[1] = saved;

    for (size_t index = 1; index < sequence_count; ++index) {
        for (unsigned int failure = 0; failure < 3; ++failure) {
            clear_fixture();
            if (failure == 0) test_reject_command = sequence[index];
            if (failure == 1) test_drop_command = sequence[index];
            if (failure == 2) test_submit_failure = sequence[index];
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(test_command_count == index + 1);
            assert(!test_link_ready && test_resets == 0);
            assert_stopped();
        }
    }
    const int getters[] = {HGIC_CMD_GET_SSID, HGIC_CMD_GET_KEY_MGMT,
        HGIC_CMD_GET_BSS_BW, HGIC_CMD_GET_CHAN_LIST};
    for (size_t i = 0; i < sizeof(getters) / sizeof(getters[0]); ++i) {
        clear_fixture();
        test_mismatch_command = getters[i];
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(!test_link_ready && test_commands[test_command_count - 1] == (unsigned)getters[i]);
        assert_stopped();
        if (getters[i] != HGIC_CMD_GET_BSS_BW) {
            clear_fixture();
            test_truncated_command = getters[i];
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(!test_link_ready);
            assert_stopped();
        }
    }
    puts("HaLow STA SSID/security/exact PSK/1 MHz/8655 configuration, readback, reset and failure cleanup passed; regulatory settings unchanged.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    test_connection_event_diagnostics();
    test_all_event_diagnostics();
#ifdef TEST_AP_INVALID_SSID
    clear_fixture();
    assert(strlen(network_ssid) == 32);
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_init_calls == 0 && test_command_count == 0);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_ARG);
    assert(test_init_calls == 0 && test_resets == 0);
    assert_stopped();
    puts("HaLow AP 32-byte SSID rejected before transport or module reset.");
    return 0;
#endif
    unsigned short frequencies[16];
    int count;
    const char *invalid[] = {"", "0", "-1", "+1", "65536", "99999999999999999999",
        "9080,", ",9080", "9080,,9160", "9080 9160", " 9080", "9080,9080",
        "9080,x", "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!parse_network_frequencies(invalid[i], frequencies, &count));
    assert(parse_network_frequencies("1,65535", frequencies, &count));
    assert(count == 2 && frequencies[0] == 1 && frequencies[1] == 65535);
    assert(parse_network_frequencies("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", frequencies, &count));
    assert(count == 16);
    clear_fixture();
    char saved_ssid = network_ssid[0];
    network_ssid[0] = '\0';
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_init_calls == 0 && test_command_count == 0);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_ARG);
    assert(test_init_calls == 0 && test_resets == 0);
    assert_stopped();
    network_ssid[0] = saved_ssid;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK
    clear_fixture();
    char saved_psk = network_psk[63];
    network_psk[63] = '\0';
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL && test_init_calls == 0);
    assert_stopped();
    network_psk[63] = 'g';
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL && test_init_calls == 0);
    assert_stopped();
    network_psk[63] = saved_psk;
#endif
    const unsigned int sequence[] = {HGIC_CMD_GET_FW_INFO, HGIC_CMD_SET_SSID,
#ifdef CONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK
        HGIC_CMD_SET_WPA_PSK,
#endif
        HGIC_CMD_SET_KEY_MGMT, HGIC_CMD_SET_BSS_BW, HGIC_CMD_SET_CHAN_LIST,
        HGIC_CMD_SET_WORK_MODE, HGIC_CMD_RADIO_ONOFF, HGIC_CMD_GET_MODE, HGIC_CMD_GET_SSID,
        HGIC_CMD_GET_KEY_MGMT, HGIC_CMD_GET_BSS_BW, HGIC_CMD_GET_CHAN_LIST};
    size_t sequence_count = sizeof(sequence) / sizeof(sequence[0]);
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(test_command_count == sequence_count);
    assert(memcmp(test_commands, sequence, sizeof(sequence)) == 0);
    assert(test_radio_enable_logs == 1);
    assert(ap_interface_ready && test_probe_count == 1);
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_resets == 1 && test_command_count == sequence_count * 2 + 1);
    assert(test_radio_enable_logs == 2);
    assert(test_commands[sequence_count] == HGIC_CMD_RESET);
    assert(memcmp(test_commands + sequence_count + 1, sequence, sizeof(sequence)) == 0);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    for (size_t i = 1; i < sequence_count; ++i) {
        for (int failure = 0; failure < 3; ++failure) {
            clear_fixture();
            if (failure == 0) test_reject_command = sequence[i];
            if (failure == 1) test_drop_command = sequence[i];
            if (failure == 2) test_submit_failure = sequence[i];
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(test_command_count == i + 1 && test_probe_count == 0 && test_resets == 0);
            assert_stopped();
        }
    }
    const int getters[] = {HGIC_CMD_GET_SSID, HGIC_CMD_GET_KEY_MGMT,
        HGIC_CMD_GET_BSS_BW, HGIC_CMD_GET_CHAN_LIST};
    for (size_t i = 0; i < sizeof(getters) / sizeof(getters[0]); ++i) {
        clear_fixture();
        test_mismatch_command = getters[i];
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_probe_count == 0 && !test_link_ready);
        assert_stopped();
        if (getters[i] != HGIC_CMD_GET_BSS_BW) {
            clear_fixture();
            test_truncated_command = getters[i];
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(test_probe_count == 0);
            assert_stopped();
        }
    }
    puts("HaLow AP configuration validation, encoded writes, readback, failure gating and reset reapplication passed.");
    return 0;
}
#else
int main(void)
{
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    assert(robusto_halow_spi_register() == ROB_OK);
    assert(test_driver.reset_and_reinitialize != NULL);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    assert(test_init_calls == 0 && test_resets == 0);

    test_connection_event_diagnostics();
    test_all_event_diagnostics();
    for (unsigned int failure = 0; failure < 3; ++failure) {
        clear_fixture();
        if (failure == 0) test_reject_command = HGIC_CMD_RADIO_ONOFF;
        if (failure == 1) test_drop_command = HGIC_CMD_RADIO_ONOFF;
        if (failure == 2) test_submit_failure = HGIC_CMD_RADIO_ONOFF;
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_command_count == 3 && test_commands[2] == HGIC_CMD_RADIO_ONOFF);
        assert(test_radio_enable_logs == 0 && test_probe_count == 0 && test_resets == 0);
        assert_stopped();
    }
    start_fixture();
    test_reject_command = HGIC_CMD_RADIO_ONOFF;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_resets == 1 && test_command_count == 8);
    assert(test_radio_enable_logs == 1);
    assert_stopped();
    clear_fixture();
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_init_calls == 2 && test_resets == 1 && test_starts == 1 && test_stops == 0);
    assert(test_command_count == 5 && test_commands[0] == 132 &&
           test_commands[1] == 43 && test_commands[2] == 41 &&
           test_commands[3] == 65 && test_commands[4] == TEST_STATUS_COMMAND);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    clear_fixture();
    test_invalid_gpio = true;
    assert(test_driver.reset_and_reinitialize(NULL) == ROB_ERR_INVALID_ARG);
    assert(test_init_calls == 0 && test_resets == 0 && test_starts == 0);
    assert_stopped();
    test_invalid_gpio = false;

    clear_fixture();
    assert(test_driver.reset_and_reinitialize(NULL) == ROB_OK);
    assert(test_resets == 1 && test_command_count == 5 && test_commands[0] == 132);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    clear_fixture();
    test_drop_command = 43;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_ticks >= COMMAND_TIMEOUT_MS && test_command_count == 1);
    assert(test_resets == 0);
    assert_stopped();
    test_reset_clears_failure = true;
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_init_calls == 3 && test_starts == 2 && test_stops == 0 && test_resets == 1);
    assert(test_command_count == 6 && test_commands[1] == 132 &&
           test_commands[2] == 43 && test_commands[3] == 41 &&
           test_commands[4] == 65 && test_commands[5] == TEST_STATUS_COMMAND);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    clear_fixture();
    test_drop_command = 43;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_init_calls == 2 && test_resets == 1 && test_starts == 1);
    assert(test_command_count == 2 && test_commands[0] == 132 && test_commands[1] == 43);
    assert_stopped();

    clear_fixture();
    test_reset_fails = true;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_init_calls == 1 && test_resets == 1 && test_starts == 0);
    assert(test_command_count == 1 && test_commands[0] == 132);
    assert_stopped();

    clear_fixture();
    test_init_failures = 1;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_init_calls == 1 && test_resets == 0 && test_starts == 0);
    assert(test_command_count == 0);
    assert_stopped();

    clear_fixture();
    test_cleanup_failures = 1;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(!running && !test_started && stopping && device != NULL);
    assert(test_resets == 1 && test_starts == 0);
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_resets == 2 && test_starts == 1 && test_init_calls == 3);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    clear_fixture();
    test_drop_command = 43;
    test_cleanup_failures = 1;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(!running && device != NULL && test_resets == 0);
    test_reset_clears_failure = true;
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_resets == 1 && test_commands[1] == 132 && test_init_calls == 3);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_link_ready = true;
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_resets == 1 && test_starts == 2 && test_stops == 1);
    assert(test_init_calls == 2 && test_command_count == 9);
    assert(test_commands[4] == 132 && test_commands[5] == 43 &&
           test_commands[6] == 41 && test_commands[7] == 65 &&
           test_commands[8] == TEST_STATUS_COMMAND);
    assert(test_uart_calls == 0 && running && !test_link_ready);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_lock_failures = 1;
    assert(reset_and_reinitialize() == ESP_ERR_TIMEOUT);
    assert(test_resets == 0 && !stopping);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_link_failures = 1;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_resets == 0 && test_starts == 1);
    assert_stopped();

    start_fixture();
    test_fail_lock_after_exit = true;
    assert(reset_and_reinitialize() == ESP_ERR_TIMEOUT);
    assert(test_resets == 0 && test_receiver_exits == 1);
    assert(robusto_halow_stop() == ROB_OK);
    assert(test_receiver_exits == 1);
    assert_stopped();

    start_fixture();
    test_current_task = rx_task;
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    test_current_task = (void *)(uintptr_t)1;
    assert(!stopping && test_resets == 0);
    test_reset_fails = true;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_resets == 1 && test_starts == 1);
    assert_stopped();

    start_fixture();
    test_reject_command = 43;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_resets == 1 && test_starts == 2 && test_init_calls == 2);
    assert(test_command_count == 6 && test_commands[5] == 43);
    assert_stopped();

    start_fixture();
    test_quiesce_timeout = true;
    assert(reset_and_reinitialize() == ESP_ERR_TIMEOUT);
    assert(test_resets == 0 && test_starts == 1 && test_stops == 0);
    assert(device != NULL && rx_task != NULL && stopping && !test_link_ready);
    test_quiesce_timeout = false;
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_no_credits = true;
    assert(reset_and_reinitialize() == ESP_ERR_TIMEOUT);
    assert(test_resets == 0 && test_starts == 1);
    assert_stopped();

    start_fixture();
    test_cleanup_failures = 1;
    assert(reset_and_reinitialize() == ESP_FAIL);
    assert(test_resets == 1 && test_starts == 1 && device != NULL);
    assert(running && test_started);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    assert(test_resets == 1);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    faulted = true;
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_resets == 1 && !faulted);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    test_reject_command = 43;
    unsigned int resets = test_resets;
    unsigned int commands = test_command_count;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_resets == resets && test_command_count == commands + 1);
    assert_stopped();
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    clear_fixture();
    test_reject_command = HGIC_CMD_SET_WORK_MODE;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_command_count == 2 && test_probe_count == 0);
    assert_stopped();
    clear_fixture();
    test_reject_command = HGIC_CMD_GET_MODE;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_command_count == 4 && test_probe_count == 0);
    assert_stopped();
    clear_fixture();
    test_bad_mode = true;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_probe_count == 0 && !test_link_ready);
    assert_stopped();
    clear_fixture();
    test_bad_probe = true;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_probe_count == 1 && !test_link_ready);
    assert_stopped();
    clear_fixture();
    test_drop_probe = true;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_ticks >= COMMAND_TIMEOUT_MS && !test_link_ready);
    assert_stopped();

    start_fixture();
    uint8_t payload[ROBUSTO_HALOW_SPI_FRAME_MAX] = {0};
    const uint8_t destination[6] = {0x96, 0xa1, 0x3e, 0x7f, 0x74, 0x38};
    tx_window = 2;
    assert(test_driver.write(NULL, destination, payload, sizeof(payload)) == sizeof(payload));
    assert(!hgic.status.conn_state);
    test_pending_event = HGIC_EVENT_DISCONECTED;
    test_driving_receiver = true;
    test_delay_limit = 2;
    receive_task(NULL);
    test_driving_receiver = false;
    assert(test_link_ups == 1 && !hgic.status.conn_state && !test_link_ready);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_change_role_after_up = test_driving_receiver = true;
    receive_task(NULL);
    test_driving_receiver = false;
    assert(test_link_ups == 1 && faulted && !ap_interface_ready && !test_link_ready);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();

    start_fixture();
    test_drop_command = HGIC_CMD_GET_MODE;
    test_driving_receiver = true;
    receive_task(NULL);
    test_driving_receiver = false;
    assert(test_link_ups == 1 && faulted && !test_link_ready);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    puts("HaLow AP role/readback/loopback, no-STA writes, disconnect independence, role/status loss and reset lifecycle assertions passed.");
#else
    puts("HaLow STA backend startup, reset, fault and cleanup lifecycle assertions passed.");
#endif
    return 0;
}
#endif
