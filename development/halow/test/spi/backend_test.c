#include "backend_test_stubs.h"
#include "halow_spi.c"
#include <stdio.h>
#include <stdarg.h>

test_semaphore_t test_semaphores[2];
unsigned int test_semaphore_count, test_uart_calls, test_init_calls;
unsigned int test_cleanup_failures;
unsigned int test_lock_failures;
TickType_t test_ticks;
static unsigned int test_random_calls;
static bool test_random_zero;

uint32_t esp_random(void)
{
    ++test_random_calls;
    return test_random_zero ? 0 : UINT32_C(0x9e3779b9) * test_random_calls;
}
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
static bool test_pending_frame;
static uint16_t test_frame_cookie;
static unsigned int test_cookie_logs;
static unsigned int test_spi_rx_logs;
static unsigned int test_spi_trace_logs;
static char test_last_spi_trace[256];
static bool test_short_read;
static unsigned int test_credit_requests, test_data_writes;
static bool test_credit_request_fails;
static uint16_t test_credit_response = 2;
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
static unsigned int test_minimum_exchanges, test_minimum_failure, test_minimum_fail_at;
static unsigned int test_minimum_verified_logs;
static bool test_minimum_report_pending, test_minimum_report_first, test_minimum_no_nul;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
static unsigned int test_sweep_pending, test_sweep_writes, test_sweep_fail_at;
static uint8_t test_sweep_packet[576];
static unsigned int test_sweep_packet_length;
static bool test_sweep_allow_normal;
static unsigned int test_sweep_normal_receives;
static char test_sweep_boundary_log[256], test_sweep_adjacent_log[256];
static unsigned int test_sweep_size_logs, test_sweep_missing_logs;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
static uint8_t test_echo_request[1664];
static unsigned int test_echo_count, test_echo_pass_logs, test_echo_failure, test_echo_fail_at;
static unsigned int test_echo_corrupt_offset;
static uint32_t test_echo_last_serial;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
static bool test_check_busy_stuck;
static unsigned int test_write_check_logs;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
static bool test_trace_spi, test_trace_spi_failure, test_trace_short_write;
static uint8_t test_trace_response = 5;
static unsigned int test_trace_logs;

int test_spi_transmit(spi_transaction_t *transaction)
{
    assert(test_trace_spi);
    const uint8_t *tx = transaction->tx_buffer;
    uint8_t *rx = transaction->rx_buffer;
    unsigned int length = transaction->length / 8U;
    if (test_trace_spi_failure && length == 69U) return ESP_FAIL;
    if (rx != NULL) {
        memset(rx, 0xff, length);
        if (length == 17U) rx[8] = 0;
        if (length == 69U && tx[0] == 0xfc) {
            rx[67] = test_trace_response;
            rx[68] = 0;
        }
        if (length == 29U) memset(rx, 0, length - 1U);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
        if (test_check_busy_stuck && length == 29U) memset(rx, 0, length);
#endif
    }
    return ESP_OK;
}
#endif

void test_log(const char *tag, const char *format, ...)
{
    (void)tag;
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    if (strstr(format, "set and independently queried/readback verified"))
        ++test_minimum_verified_logs;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
    if (strncmp(format, "SPI_ECHO PASS ", 14) == 0) ++test_echo_pass_logs;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
    if (strncmp(format, "SDSPI_CHECK ", 12) == 0) ++test_write_check_logs;
#endif
#if defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE)
    assert(strcmp(format, "Stored RF/security settings unchanged") != 0);
#endif
    va_list all_arguments;
    va_start(all_arguments, format);
    int log_size = vsnprintf(test_last_log, sizeof(test_last_log), format, all_arguments);
    va_end(all_arguments);
    assert(log_size >= 0 && (size_t)log_size < sizeof(test_last_log));
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    if (strncmp(format, "FRM2_SWEEP RX size=", 19) == 0) ++test_sweep_size_logs;
    if (strncmp(format, "FRM2_SWEEP MISSING ", 19) == 0) ++test_sweep_missing_logs;
    if (strstr(test_last_log, "FRM2_BYTE_SCAN boundary ") == test_last_log ||
        strstr(test_last_log, "FRM2_TIGHT_SCAN boundary ") == test_last_log) {
        assert(strlen(test_last_log) < sizeof(test_sweep_boundary_log));
        strcpy(test_sweep_boundary_log, test_last_log);
    }
    if (strstr(test_last_log, "FRM2_BYTE_SCAN adjacent ") == test_last_log ||
        strstr(test_last_log, "FRM2_TIGHT_SCAN adjacent ") == test_last_log) {
        assert(strlen(test_last_log) < sizeof(test_sweep_adjacent_log));
        strcpy(test_sweep_adjacent_log, test_last_log);
    }
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
    if (strncmp(format, "SPI_ECHO FAIL ", 14) == 0)
        fprintf(stderr, "%s\n", test_last_log);
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (strstr(format, "TXTRACE") != NULL) {
        assert(test_semaphores[0].count == 1 && tx_trace_current == NULL);
        ++test_trace_logs;
    }
#endif
#ifdef NETWORK_WPA_PSK
    if (strlen(network_psk) == 64) assert(strstr(test_last_log, network_psk) == NULL);
#endif
    if (strstr(format, "Radio enable command 65 acknowledged") != NULL)
        ++test_radio_enable_logs;
    if (strstr(format, "RX cookie discontinuity:") != NULL)
        ++test_cookie_logs;
    if (strstr(format, "RX SPI data frames=") != NULL)
        ++test_spi_rx_logs;
    if (strstr(format, "RXTRACE SPI") != NULL) {
        ++test_spi_trace_logs;
        assert(strlen(test_last_log) < sizeof(test_last_spi_trace));
        strcpy(test_last_spi_trace, test_last_log);
    }
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
{
    (void)source; (void)frame; (void)length; (void)rssi;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    assert(test_sweep_allow_normal);
    ++test_sweep_normal_receives;
    return ROB_OK;
#else
    assert(false);
    return ROB_FAIL;
#endif
}

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
        ++test_credit_requests;
        if (test_credit_request_fails) return -1;
        test_pending_command = -1;
        return length;
    }
    if (data[2] == HGIC_HDR_TYPE_TEST2) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
        if (length != sizeof(struct hgic_hdr) + 4) {
            const unsigned int totals[] = {278, 406, 470, 512, 513, 534, 1046};
            assert(!running && !test_link_ready && rx_task == NULL);
            assert(length == totals[(test_echo_count % 70U) / 10U]);
            assert(memcmp(data + sizeof(struct hgic_hdr), "RHEC", 4) == 0);
            uint32_t serial = get_unaligned_le32(data + sizeof(struct hgic_hdr) + 4);
            assert(serial > test_echo_last_serial);
            test_echo_last_serial = serial;
            ++test_echo_count;
            memcpy(test_echo_request, data, length);
            if (test_echo_failure == 4 && test_echo_count == test_echo_fail_at) return -1;
            test_pending_command = -3;
            return length;
        }
#endif
        assert(length == sizeof(struct hgic_hdr) + 4);
        assert(memcmp(data + sizeof(struct hgic_hdr), ROBUSTO_HALOW_SPI_AP_PROBE, 4) == 0);
        ++test_probe_count;
        test_pending_command = -2;
        return length;
    }
    if (data[2] == HGIC_HDR_TYPE_FRM2) {
        assert(length <= 1664);
        ++test_data_writes;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
        if (length >= 30U && frm2_sweep_candidate(data + 22, length - 22U)) {
            ++test_sweep_writes;
            assert(length <= sizeof(test_sweep_packet));
            memcpy(test_sweep_packet, data, length);
            test_sweep_packet_length = length;
            if (test_sweep_writes == test_sweep_fail_at) return (int)length - 1;
        }
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        if (test_trace_spi) {
            unsigned int blocks = (length + 63U) / 64U;
            uint8_t command[17] = {0xff, 0x75, 0x9c, 0, 0, 0};
            uint8_t response[17];
            command[4] = (uint8_t)(blocks >> 8);
            command[5] = (uint8_t)blocks;
            spidrv_write_read(NULL, command, response, sizeof(command));
            for (unsigned int i = 0; i < blocks; ++i) {
                uint8_t block[69] = {0xfc}, block_response[69];
                unsigned int remaining = length - i * 64U;
                memcpy(block + 1, data + i * 64U, remaining < 64U ? remaining : 64U);
                spidrv_write_read(NULL, block, block_response, sizeof(block));
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
                uint8_t block_busy[29];
                spidrv_read(NULL, block_busy, sizeof(block_busy), 0);
#endif
            }
            uint8_t stop = 0xfd, busy[29];
            spidrv_write(NULL, &stop, 1, 0);
            spidrv_read(NULL, busy, sizeof(busy), 0);
            if (test_trace_short_write) return (int)length - 1;
        }
#endif
        return length;
    }
    unsigned int command_id = data[8];
    assert(test_command_count < sizeof(test_commands) / sizeof(test_commands[0]));
    test_commands[test_command_count++] = command_id;
    if ((int)command_id == test_submit_failure) return -1;
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    if (command_id == HGIC_CMD_SET_ATCMD) {
        ++test_minimum_exchanges;
        const char *expected = test_minimum_exchanges % 2
                                   ? minimum_mcs_set : minimum_mcs_query;
        assert(length == CMD_HDR_LEN + strlen(expected));
        assert(memcmp(data + CMD_HDR_LEN, expected, strlen(expected)) == 0);
        assert(!running && !test_link_ready && rx_task == NULL);
        if (test_minimum_failure == 8 && test_minimum_exchanges == test_minimum_fail_at)
            return -1;
        test_minimum_report_pending = true;
    }
#endif
    if (command_id == HGIC_CMD_SET_FRG_THRESHOLD) {
        assert(!"Removed fragment-threshold experiment must not send command 10");
    }
    if (command_id == HGIC_CMD_RADIO_ONOFF) {
        assert(length == CMD_HDR_LEN + 1);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START
        bool first = test_command_count == 1 ||
                     test_commands[test_command_count - 2] == HGIC_CMD_RESET;
        assert(data[CMD_HDR_LEN] == (first ? 0 : 1));
#else
        assert(data[CMD_HDR_LEN] == 1);
#endif
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
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    if (test_sweep_pending) {
        assert(capacity >= 576U);
        memset(data, 0, capacity);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
        data[2] = HGIC_HDR_TYPE_FRM2;
        data[14] = 2;
        data[20] = 0x66;
        data[21] = 0xaa;
        unsigned int raw = frm2_sweep_build(data + 22, 77, 1, FRM2_SWEEP_COARSE);
        put_unaligned_le16(raw + 22U, data + 4);
        test_sweep_pending = 0;
        return (int)((raw + 22U + 63U) & ~63U);
    }
#endif
    if (test_short_read) {
        test_short_read = false;
        data[0] = 0;
        return 1;
    }
    if (test_pending_frame) {
        assert(capacity >= 24);
        memset(data, 0, capacity);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
        data[2] = HGIC_HDR_TYPE_FRM2;
        put_unaligned_le16(23, data + 4);
        put_unaligned_le16(test_frame_cookie, data + 6);
        data[14] = 0x12;
        data[20] = 0x66;
        data[21] = 0xaa;
        data[22] = 'R';
        test_pending_frame = false;
        return 24;
    }
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
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    bool minimum_fails = test_minimum_exchanges == test_minimum_fail_at;
    if (test_minimum_report_pending &&
        (test_minimum_report_first || test_pending_command == 0)) {
        test_minimum_report_pending = false;
        if (minimum_fails && test_minimum_failure == 1) return 0;
        if (minimum_fails && test_minimum_failure == 9) return -1;
        memset(data, 0, capacity);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
        data[2] = HGIC_HDR_TYPE_EVENT;
        data[8] = HGIC_EVENT_FWDBG_INFO;
        unsigned int size = sizeof(minimum_mcs_report) - (test_minimum_no_nul ? 1U : 0U);
        memcpy(data + CMD_HDR_LEN, minimum_mcs_report, size);
        if (minimum_fails && test_minimum_failure == 2)
            data[CMD_HDR_LEN + sizeof("limit tx mcs min= ") - 1] = '0';
        if (minimum_fails && test_minimum_failure == 3)
            data[CMD_HDR_LEN + size - 1] = 'x';
        if (minimum_fails && test_minimum_failure == 4) size = 17;
        if (minimum_fails && test_minimum_failure == 5) data[CMD_HDR_LEN] = 'x';
        put_unaligned_le16(CMD_HDR_LEN + size, data + 4);
        return CMD_HDR_LEN + size;
    }
    if (minimum_fails && test_minimum_failure == 6 &&
        test_pending_command == HGIC_CMD_SET_ATCMD) {
        test_pending_command = 0;
        return 0;
    }
#endif
    if (test_pending_command == 0) return 0;
    if (test_pending_command == test_drop_command) return 0;
    memset(data, 0, capacity);
    put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
    if (test_pending_command == -3) {
        bool fail = test_echo_failure != 0 && test_echo_count == test_echo_fail_at;
        if (fail && test_echo_failure == 3) return 0;
        if (fail && test_echo_failure == 5) return -1;
        unsigned int length = get_unaligned_le16(test_echo_request + 4);
        memcpy(data, test_echo_request, length);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, data);
        if (fail && test_echo_failure == 1) {
            unsigned int offset = test_echo_corrupt_offset == UINT32_MAX
                                      ? length - 1U : sizeof(struct hgic_hdr) + test_echo_corrupt_offset;
            assert(offset < length);
            data[offset] ^= 1;
        }
        if (fail && test_echo_failure == 2) {
            --length;
            put_unaligned_le16(length, data + 4);
        }
        test_pending_command = 0;
        return (length + 63U) & ~63U;
    }
#endif
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
        put_unaligned_le16(test_credit_response, data + 6);
        test_pending_command = 0;
        return sizeof(struct hgic_hdr);
    }
    unsigned int id = test_pending_command;
    data[2] = HGIC_HDR_TYPE_CMD;
    data[8] = id;
    unsigned int length = CMD_HDR_LEN;
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    if (id == HGIC_CMD_SET_ATCMD && minimum_fails && test_minimum_failure == 7) {
        put_unaligned_le16((uint16_t)-1, data + 10);
    } else
#endif
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
    test_pending_frame = false;
    test_frame_cookie = 0;
    test_cookie_logs = 0;
    test_spi_rx_logs = 0;
    test_spi_trace_logs = 0;
    test_short_read = false;
    rx_trace_reads = 0;
    bitrate_logs = signal_logs = 0;
    test_terminated_ssid = false;
    test_mismatch_command = test_truncated_command = test_submit_failure = 0;
    test_probe_count = test_link_ups = test_delay_count = test_delay_limit = 0;
    test_ticks = 0;
    test_random_calls = 0;
    test_random_zero = false;
    test_credit_requests = test_data_writes = 0;
    test_credit_request_fails = false;
    test_credit_response = 2;
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    test_minimum_exchanges = test_minimum_failure = test_minimum_fail_at = 0;
    test_minimum_verified_logs = 0;
    test_minimum_report_pending = test_minimum_report_first = test_minimum_no_nul = false;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    test_sweep_pending = test_sweep_writes = test_sweep_fail_at = 0;
    test_sweep_packet_length = 0;
    test_sweep_allow_normal = false;
    test_sweep_normal_receives = 0;
    test_sweep_boundary_log[0] = test_sweep_adjacent_log[0] = '\0';
    test_sweep_size_logs = test_sweep_missing_logs = 0;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
    test_echo_count = test_echo_pass_logs = test_echo_failure = test_echo_fail_at = 0;
    test_echo_corrupt_offset = 0;
    test_echo_last_serial = 0;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    memset(&tx_trace, 0, sizeof(tx_trace));
    tx_trace_current = NULL;
    test_trace_spi = test_trace_spi_failure = test_trace_short_write = false;
    test_trace_response = 5;
    test_trace_logs = 0;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
    memset(&write_check, 0, sizeof(write_check));
    checked_writes = checked_block_sizes = 0;
    test_check_busy_stuck = false;
    test_write_check_logs = 0;
#endif
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
static void trace_fixture(void)
{
    clear_fixture();
    assert(initialize_transport() == ESP_OK);
    running = true;
    stopping = false;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    ap_interface_ready = true;
#else
    hgic.status.conn_state = 1;
#endif
    test_trace_spi = true;
}

static unsigned int trace_frame(uint8_t *frame, uint32_t hash, uint32_t count,
                                uint32_t index, bool request, unsigned int bytes)
{
    unsigned int length = request ? 54U : 42U + bytes;
    memset(frame, 0, length);
    frame[0] = 'R'; frame[1] = 'H'; frame[2] = 1;
    frame[3] = request ? 1U : 0U;
    put_unaligned_le32(request ? 3749U : 3750U + index, frame + 4);
    frame[14] = (uint8_t)(length - 20U);
    frame[15] = (uint8_t)((length - 20U) >> 8);
    frame[24] = MSG_FRAGMENTED;
    frame[25] = 0x80U | (request ? FRAG_REQUEST : FRAG_MESSAGE);
    put_unaligned_le32(1, frame + 26);
    if (request) {
        put_unaligned_le32(55002U, frame + 38);
        put_unaligned_le32(count, frame + 42);
        put_unaligned_le32(982U, frame + 46);
        put_unaligned_le32(hash, frame + 50);
    } else {
        put_unaligned_le32(hash, frame + 20);
        put_unaligned_le32(index, frame + 38);
    }
    return length;
}

static void finish_trace_fixture(void)
{
    assert(tx_trace_current == NULL);
    running = false;
    ap_interface_ready = false;
    hgic.status.conn_state = 0;
    release_hardware();
}

static void test_fragment_tx_trace(void)
{
    const uint8_t destination[6] = {2, 1, 2, 3, 4, 5};
    uint8_t frame[ROBUSTO_HALOW_FRAME_MAX];
    const uint32_t hash = 4212717062U;
    trace_fixture();
    unsigned int length = trace_frame(frame, hash, 57, 0, true, 0);
    tx_window = 2;
    assert(backend_write(NULL, destination, frame, length) == (int)length);
    assert(tx_trace.armed && tx_trace.request.vendor_result == 76);
    assert(tx_trace.request.blocks == 2 && test_trace_logs == 0);
    for (unsigned int index = 0; index < 57; ++index) {
        length = trace_frame(frame, hash, 57, index, false, index == 56 ? 10 : 982);
        if (index == 0) {
            tx_window = 1;
            assert(backend_write(NULL, destination, frame, length) == ROB_ERR_QUEUE_FULL);
            assert(tx_trace.fragments[0].busy == 1 && !tx_trace.fragments[0].vendor_called);
            put_unaligned_le32(3751U, frame + 4);
        }
        test_trace_response = index == 17 ? 0x0b : 5;
        tx_window = 2;
        assert(backend_write(NULL, destination, frame, length) == (int)length);
        const tx_trace_record_t *record = &tx_trace.fragments[index];
        assert(record->commands == 1 && record->blocks == (index == 56 ? 2U : 17U));
        assert(record->declared_blocks == record->blocks && record->spi_failures == 0);
        assert(record->vendor_result == (int)length + 22 && record->backend_result == (int)length);
        assert(record->sdspi_result == (int)length + 22 && record->raw_called);
        assert(record->raw_bytes == length + 22U && record->header_bytes == length + 22U);
        assert(record->ifidx == 1 && record->wire_header_seen && record->wire_header_match);
        assert(record->window_before == 2 && record->window_after == 1);
        assert(record->stops == 1 && record->busy_reads == 1);
        assert(record->busy_zero == 28 && record->busy_nonzero == 1);
        assert(record->block_response[0] == test_trace_response && record->block_busy[0] == 0);
        assert(record->command_response[1] == 0);
        assert(test_trace_logs == 0);
    }
    assert(tx_trace.fragments[0].first_sequence == 3750 && tx_trace.fragments[0].sequence == 3751);
    assert(tx_trace.fragments[56].frame_bytes == 52 && tx_trace.fragments[56].vendor_result == 74);
    robusto_halow_spi_dump_tx_trace();
    assert(tx_trace.dumped && test_trace_logs == 117);
    unsigned int logs = test_trace_logs;
    robusto_halow_spi_dump_tx_trace();
    assert(test_trace_logs == logs);
    tx_window = 2;
    assert(backend_write(NULL, destination, frame, length) == (int)length);
    assert(tx_trace.fragments[56].calls == 1);
    finish_trace_fixture();

    for (unsigned int failure = 0; failure < 3; ++failure) {
        trace_fixture();
        length = trace_frame(frame, hash, 57, 0, true, 0);
        tx_window = 2;
        assert(backend_write(NULL, destination, frame, length) == (int)length);
        length = trace_frame(frame, hash, 57, 0, false, 982);
        test_trace_spi_failure = failure == 0;
        test_trace_short_write = failure == 1;
        tx_window = failure == 2 ? 1 : 2;
        assert(backend_write(NULL, destination, frame, length) ==
               (failure == 2 ? ROB_ERR_QUEUE_FULL : ROB_ERR_SEND_FAIL));
        const tx_trace_record_t *record = &tx_trace.fragments[0];
        assert(record->spi_failures == (failure == 0 ? 1U : 0U));
        assert(record->spi_result == (failure == 0 ? ESP_FAIL : ESP_OK));
        assert(record->vendor_called == (failure != 2));
        assert(record->busy == (failure == 2 ? 1U : 0U));
        robusto_halow_spi_dump_tx_trace();
        assert(test_trace_logs == 5 && tx_trace.dumped);
        finish_trace_fixture();
    }

    trace_fixture();
    length = trace_frame(frame, hash, 57, 0, true, 0);
    test_trace_spi_failure = true;
    tx_window = 2;
    assert(backend_write(NULL, destination, frame, length) == ROB_ERR_SEND_FAIL);
    assert(tx_trace.armed && tx_trace.request.spi_failures == 1);
    robusto_halow_spi_dump_tx_trace();
    assert(tx_trace.dumped && test_trace_logs == 3 && tx_trace.fragments[0].calls == 0);
    finish_trace_fixture();

    trace_fixture();
    length = trace_frame(frame, hash, 65, 0, true, 0);
    tx_window = 2;
    assert(backend_write(NULL, destination, frame, length) == (int)length);
    length = trace_frame(frame, hash, 65, 64, false, 10);
    tx_window = 2;
    assert(backend_write(NULL, destination, frame, length) == (int)length);
    assert(tx_trace.unrecorded_calls == 1);
    tx_trace_current = &tx_trace.request;
    uint8_t block[69] = {0xfc}, response[69] = {0};
    for (unsigned int i = 0; i < TX_TRACE_BLOCKS + 1U; ++i)
        tx_trace_transfer(block, response, sizeof(block), ESP_OK);
    tx_trace_current = NULL;
    assert(tx_trace.request.blocks == 29 && tx_trace.request.block_overflow == 3);
    robusto_halow_spi_dump_tx_trace();
    assert(tx_trace.dumped);
    finish_trace_fixture();
    puts("TXTRACE v2 first pass: all 57 indices/final 10 bytes, raw block responses, busy/SPI/short-write failures, bounds and unlocked one-shot dump passed.");
}
#endif

static void test_tx_credit_backpressure(void)
{
    clear_fixture();
    assert(initialize_transport() == ESP_OK);
    running = true;
    stopping = false;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    ap_interface_ready = true;
#else
    hgic.status.conn_state = 1;
#endif
    uint8_t destination[6] = {2, 1, 2, 3, 4, 5};
    uint8_t frame[ROBUSTO_HALOW_FRAME_MAX] = {0};
    uint8_t expected_wire[1664];
    const uint32_t lengths[] = {1, 256, 512, 1024, ROBUSTO_HALOW_FRAME_MAX};
    const uint8_t source[6] = {2, 6, 7, 8, 9, 10};
    memcpy(hgic.addr, source, sizeof(source));
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)(i * 37U + 11U);
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        uint32_t length = lengths[i];
        tx_window = 2;
        hgic.txcookie = 17;
        assert(hgic_raw_send(destination, frame, length) == (int)(length + 22U));
        memcpy(expected_wire, hgic.tx_buf, length + 22U);
        tx_window = 2;
        hgic.txcookie = 17;
        assert(backend_write(NULL, destination, frame, length) == (int)length);
        assert(memcmp(expected_wire, hgic.tx_buf, length + 22U) == 0);
        assert(memcmp(hgic.tx_buf + 8, destination, 6) == 0);
        assert(memcmp(hgic.tx_buf + 14, source, 6) == 0);
        assert(hgic.tx_buf[20] == 0x66 && hgic.tx_buf[21] == 0xaa);
        assert(memcmp(hgic.tx_buf + 22, frame, length) == 0);
    }
    test_data_writes = 0;
    assert(backend_write(NULL, NULL, frame, 1) == ROB_ERR_INVALID_ARG);
    assert(backend_write(NULL, destination, NULL, 1) == ROB_ERR_INVALID_ARG);
    assert(backend_write(NULL, destination, frame, 0) == ROB_ERR_INVALID_ARG);
    assert(backend_write(NULL, destination, frame, sizeof(frame) + 1) == ROB_ERR_INVALID_ARG);
    assert(test_data_writes == 0);
    puts("Raw TX: vendor wrapping, headers and payload verified at 1/256/512/1024/1642 bytes; invalid inputs rejected.");
    tx_window = 1;
    assert(backend_write(NULL, destination, frame, sizeof(frame)) == ROB_ERR_QUEUE_FULL);
    assert(test_credit_requests == 1 && test_data_writes == 0 && tx_window == 1);
    assert(window_pending && !faulted);
    assert(backend_write(NULL, destination, frame, sizeof(frame)) == ROB_ERR_QUEUE_FULL);
    assert(test_credit_requests == 1 && test_data_writes == 0 && !faulted);
    robusto_halow_spi_packet_t packet;
    test_credit_response = 1;
    assert(read_packet(&packet) == 1);
    assert(!window_pending && tx_window == 1);
    assert(backend_write(NULL, destination, frame, sizeof(frame)) == ROB_ERR_QUEUE_FULL);
    assert(test_credit_requests == 2 && test_data_writes == 0 && !faulted);
    test_credit_response = 2;
    assert(read_packet(&packet) == 1);
    assert(!window_pending && tx_window == 2);
    assert(backend_write(NULL, destination, frame, sizeof(frame)) == sizeof(frame));
    assert(tx_window == 1 && test_data_writes == 1);
    test_credit_request_fails = true;
    assert(backend_write(NULL, destination, frame, sizeof(frame)) == ROB_ERR_SEND_FAIL);
    assert(test_credit_requests == 3 && test_data_writes == 1 && faulted);
    running = false;
    ap_interface_ready = false;
    hgic.status.conn_state = 0;
    release_hardware();
    puts("TX credits: explicit busy, one pending query, replenish/retry, and query failure passed.");
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

static void receive_cookie(uint16_t cookie, bool discontinuity)
{
    robusto_halow_spi_packet_t packet;
    unsigned int previous_logs = test_cookie_logs;
    test_frame_cookie = cookie;
    test_pending_frame = true;
    assert(read_packet(&packet) == 1);
    assert(packet.cookie_discontinuity == discontinuity);
    assert(packet.cookie_received == cookie && hgic.rxcookie == cookie);
    assert(packet.length == 1 && packet.frame[0] == 'R' && packet.source[0] == 0x12);
    assert(test_cookie_logs == previous_logs + (discontinuity ? 1U : 0U));
}

static void test_rx_cookie_diagnostics(void)
{
    clear_fixture();
    robusto_halow_spi_reset_rx_cookie();
    receive_cookie(0, false);
    assert(test_spi_rx_logs == 1);
    receive_cookie(1, false);
    receive_cookie(3, true);
    assert(strcmp(test_last_log,
        "RX cookie discontinuity: previous=1 expected=2 received=3; frame retained") == 0);
    receive_cookie(3, true);
    receive_cookie(2, true);
    for (uint16_t next = 3; next <= 193; ++next) receive_cookie(next, false);
    assert(rx_data_frames == 196 && test_spi_rx_logs == 4);
    assert(test_spi_trace_logs == 129);
    robusto_halow_rx_stats_t stats = {0};
    assert(test_driver.get_rx_stats != NULL);
    assert(test_driver.get_rx_stats(test_driver.context, &stats) == ROB_OK);
    assert(stats.spi_frames == 196 && stats.spi_unready == 0);
    rx_unready_frames = 7;
    assert(test_driver.get_rx_stats(test_driver.context, &stats) == ROB_OK);
    assert(stats.spi_frames == 196 && stats.spi_unready == 7);
    rx_unready_frames = 0;
    test_lock_failures = 1;
    assert(test_driver.get_rx_stats(test_driver.context, &stats) == ROB_ERR_MUTEX);
    assert(test_lock_failures == 0);
    rx_trace_reads = 0;
    test_short_read = true;
    robusto_halow_spi_packet_t packet;
    assert(read_packet(&packet) == 0);
    assert(test_spi_trace_logs == 130);
    assert(strstr(test_last_spi_trace, "unvalidated=1 bytes=1 header=0") != NULL);

    assert(initialize_transport() == ESP_OK);
    receive_cookie(65535, false);
    receive_cookie(0, false);
    assert(release_hardware() == ESP_OK);
    assert_stopped();
    assert(test_spi_rx_logs == 5);
    assert(initialize_transport() == ESP_OK);
    receive_cookie(0, false);
    assert(release_hardware() == ESP_OK);
    assert_stopped();
    assert(test_spi_rx_logs == 6);
    assert(test_driver.get_rx_stats(test_driver.context, &stats) == ROB_OK);
    assert(stats.spi_frames == 1 && stats.spi_unready == 0);
    clear_fixture();
    puts("HaLow RX cookie baseline, wraparound, discontinuity delivery/logging and transport reinitialization passed.");
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

#if defined(TEST_MINIMUM_MCS)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    for (unsigned int order = 0; order < 2; ++order) {
        for (unsigned int termination = 0; termination < 2; ++termination) {
            clear_fixture();
            test_minimum_report_first = order != 0;
            test_minimum_no_nul = termination != 0;
            assert(robusto_halow_start() == ROB_OK);
            assert(test_minimum_exchanges == 2);
            assert(test_minimum_verified_logs == 1);
            const uint8_t destination[6] = {2, 1, 2, 3, 4, 5};
            uint8_t frame[ROBUSTO_HALOW_FRAME_MAX] = {0};
            hgic.status.conn_state = 1;
            tx_window = 2;
            assert(backend_write(NULL, destination, frame, sizeof(frame)) == sizeof(frame));
            assert(reset_and_reinitialize() == ESP_OK);
            assert(test_minimum_exchanges == 4);
            assert(test_minimum_verified_logs == 2);
            assert(robusto_halow_stop() == ROB_OK);
            assert_stopped();
        }
    }
    for (unsigned int stage = 1; stage <= 2; ++stage) {
        for (unsigned int failure = 1; failure <= 9; ++failure) {
            clear_fixture();
            test_minimum_fail_at = stage;
            test_minimum_failure = failure;
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(test_minimum_exchanges == stage);
            assert(!test_link_ready && test_resets == 0);
            assert_stopped();
        }
    }
    puts("Minimum MCS: exact set/query, acknowledgement/report in both orders, optional NUL, reset reapplication, full 1642-byte TX and explicit failures passed.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX)
static void test_sweep_wire(void)
{
    uint8_t frame[513];
    const uint64_t identity = UINT64_C(0xfedcba9876543210);
    const unsigned int sizes[] = {256, 384, 448, 490, 491, 512};
    for (uint16_t sequence = 0; sequence <= FRM2_SWEEP_END; ++sequence) {
        unsigned int length = frm2_sweep_build(frame, identity, sequence, FRM2_SWEEP_COARSE);
        assert(length == ((sequence == 0 || sequence == 61) ? 24U :
                         sizes[(sequence - 1U) / 10U]));
        assert(frm2_sweep_candidate(frame, length));
        uint64_t run;
        uint16_t decoded;
        assert(frm2_sweep_validate(frame, length, &run, &decoded) == -1);
        assert(run == identity && decoded == sequence);
        for (unsigned int i = 0; i < length; ++i) {
            frame[i] ^= 1;
            assert(frm2_sweep_validate(frame, length, &run, &decoded) >= 0);
            if (i < 8U) assert(!frm2_sweep_candidate(frame, length));
            frame[i] ^= 1;
        }
        assert(frm2_sweep_validate(frame, length - 1U, &run, &decoded) >= 0);
        assert(frm2_sweep_validate(frame, length + 1U, &run, &decoded) >= 0);
    }
    uint64_t run;
    uint16_t sequence;
    frm2_sweep_build(frame, 0, 1, FRM2_SWEEP_COARSE);
    assert(frm2_sweep_validate(frame, 256, &run, &sequence) == 8);
    frm2_sweep_build(frame, identity, 1, FRM2_SWEEP_COARSE);
    frame[16] = 62;
    assert(frm2_sweep_validate(frame, 256, &run, &sequence) == 16);
    assert(frm2_sweep_length(62, FRM2_SWEEP_COARSE, 0) == 0);
}

static void test_sweep_receiver(void)
{
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5};
    const uint8_t other[6] = {2, 1, 2, 3, 4, 6};
    uint8_t frame[512];
    frm2_sweep_rx_t state = {0};
    assert(!frm2_sweep_receive(&state, source, (const uint8_t *)"RH", 2, 1));
    frm2_sweep_poll(&state, FRM2_SWEEP_TIMEOUT_US);
    assert(!state.active && !state.reported);
    for (uint16_t seq = 0; seq <= FRM2_SWEEP_END; ++seq) {
        unsigned int length = frm2_sweep_build(frame, 77, seq, FRM2_SWEEP_COARSE);
        assert(frm2_sweep_receive(&state, source, frame, length,
                                  1 + seq * FRM2_SWEEP_INTERVAL_US));
    }
    assert(state.seen[0] == ((UINT64_C(1) << 60) - 1U));
    frm2_sweep_poll(&state, state.ended + FRM2_SWEEP_GRACE_US - 1);
    assert(!state.reported);
    frm2_sweep_poll(&state, state.ended + FRM2_SWEEP_GRACE_US);
    assert(state.reported && strstr(test_last_log, "RX PASS") != NULL);
    assert(frm2_sweep_receive(&state, source, frame, 24, state.ended + 3000000LL));
    assert(strstr(test_last_log, "late packet") != NULL && state.duplicates == 0);

    memset(&state, 0, sizeof(state));
    for (uint16_t seq = 0; seq <= FRM2_SWEEP_END; ++seq) {
        unsigned int length = frm2_sweep_build(frame, 78, seq, FRM2_SWEEP_COARSE);
        if (seq == 31) continue;
        if (seq == 51) frame[length - 1] ^= 1;
        assert(frm2_sweep_receive(&state, source, frame, length, 1 + seq * 1000000LL));
    }
    assert(state.corrupt == 1);
    frm2_sweep_build(frame, 78, 1, FRM2_SWEEP_COARSE);
    assert(frm2_sweep_receive(&state, source, frame, 256, 63000000LL));
    assert(state.duplicates == 1);
    uint64_t seen = state.seen[0];
    assert(frm2_sweep_receive(&state, other, frame, 256, 63000000LL));
    assert(state.seen[0] == seen && strstr(test_last_log, "unexpected source") != NULL);
    frm2_sweep_build(frame, 79, 1, FRM2_SWEEP_COARSE);
    assert(frm2_sweep_receive(&state, source, frame, 256, 63000000LL));
    assert(state.run == 78 && state.seen[0] == seen);
    frm2_sweep_poll(&state, state.ended + FRM2_SWEEP_GRACE_US);
    assert(state.reported && strstr(test_last_log, "RX FAIL") != NULL);
    assert(strstr(test_last_log, "verified=58/60") != NULL);

    for (unsigned int missing_control = 0; missing_control < 2; ++missing_control) {
        memset(&state, 0, sizeof(state));
        for (uint16_t seq = 0; seq <= FRM2_SWEEP_END; ++seq) {
            if (seq == (missing_control ? FRM2_SWEEP_END : 0)) continue;
            unsigned int length = frm2_sweep_build(frame, 80, seq, FRM2_SWEEP_COARSE);
            assert(frm2_sweep_receive(&state, source, frame, length, 1 + seq * 1000000LL));
        }
        frm2_sweep_poll(&state, state.began + FRM2_SWEEP_TIMEOUT_US);
        assert(state.reported && strstr(test_last_log, "RX FAIL") != NULL);
        assert(strstr(test_last_log, "verified=60/60") != NULL);
    }
}

static unsigned int test_byte_scan_length(unsigned int sequence)
{
    assert(sequence <= 388U);
    if (sequence == 0 || sequence == 388U) return 24U;
    if (sequence <= 129U) return 255U + sequence;
    if (sequence <= 258U) return 514U - sequence;
    return sequence - 3U;
}

static void test_byte_scan_wire(void)
{
    uint8_t frame[513];
    unsigned int counts[129] = {0};
    assert(frm2_sweep_count(FRM2_SWEEP_BYTE_SCAN) == 387 &&
           frm2_sweep_end(FRM2_SWEEP_BYTE_SCAN) == 388);
    for (uint16_t seq = 0; seq <= 388U; ++seq) {
        unsigned int length = frm2_sweep_build(frame, UINT64_C(0xfedcba9876543210),
                                              seq, FRM2_SWEEP_BYTE_SCAN);
        assert(length == test_byte_scan_length(seq));
        assert(frm2_sweep_candidate(frame, length) && frame[7] == '2');
        uint64_t run;
        uint16_t decoded;
        assert(frm2_sweep_validate(frame, length, &run, &decoded) == -1);
        assert(run == UINT64_C(0xfedcba9876543210) && decoded == seq);
        if (seq && seq != 388U) ++counts[length - 256U];
        if (seq % 64U == 0 || seq == 1 || seq == 129 || seq == 130 ||
            seq == 258 || seq == 259 || seq == 387 || seq == 388) {
            for (unsigned int i = 0; i < length; ++i) {
                frame[i] ^= 1;
                assert(frm2_sweep_validate(frame, length, &run, &decoded) >= 0);
                frame[i] ^= 1;
            }
        }
        assert(frm2_sweep_validate(frame, length - 1U, &run, &decoded) >= 0);
        assert(frm2_sweep_validate(frame, length + 1U, &run, &decoded) >= 0);
    }
    for (unsigned int i = 0; i < 129U; ++i) assert(counts[i] == 3);
    assert(frm2_sweep_length(389, FRM2_SWEEP_BYTE_SCAN, 0) == 0);
    uint64_t run;
    uint16_t decoded;
    frm2_sweep_build(frame, 99, 388, FRM2_SWEEP_BYTE_SCAN);
    frame[16] = 133; /* 389 little endian. */
    assert(frm2_sweep_validate(frame, 24, &run, &decoded) == 16);
}

static frm2_sweep_rx_t test_byte_scan_mask(unsigned int cutoff)
{
    frm2_sweep_rx_t state = {.active = true, .start = true, .end = true,
                             .mode = FRM2_SWEEP_BYTE_SCAN, .run = 99};
    for (unsigned int seq = 1; seq <= 387U; ++seq) {
        if (test_byte_scan_length(seq) >= cutoff) continue;
        unsigned int index = seq - 1U;
        state.seen[index / 64U] |= UINT64_C(1) << (index % 64U);
    }
    return state;
}

static void test_byte_scan_boundaries(void)
{
    for (unsigned int cutoff = 257; cutoff <= 384U; ++cutoff) {
        frm2_sweep_rx_t state = test_byte_scan_mask(cutoff);
        unsigned int sizes_before = test_sweep_size_logs, missing_before = test_sweep_missing_logs;
        frm2_sweep_report(&state, "end+grace");
        assert(test_sweep_size_logs == sizes_before + 129U);
        assert(test_sweep_missing_logs == missing_before + (385U - cutoff) * 3U);
        char expected[96];
        snprintf(expected, sizeof(expected), "established=1 last_full=%u first_missing=%u",
                 cutoff - 1U, cutoff);
        assert(strstr(test_sweep_boundary_log, expected) != NULL);
        assert(strstr(test_sweep_boundary_log, "mixed_sizes=0 monotonic=1") != NULL);
        if (cutoff == 299)
            assert(strstr(test_sweep_adjacent_log, "raw=298/299 vendor=320/321 blocks=5/6"));
        if (cutoff == 363)
            assert(strstr(test_sweep_adjacent_log, "raw=362/363 vendor=384/385 blocks=6/7"));
    }
    for (unsigned int condition = 0; condition < 10U; ++condition) {
        frm2_sweep_rx_t state = test_byte_scan_mask(299);
        if (condition == 0) state = test_byte_scan_mask(385); /* All pass. */
        if (condition == 1) state = test_byte_scan_mask(256); /* All missing. */
        if (condition == 2) state.start = false;
        if (condition == 3) state.corrupt = 1;
        if (condition == 4) state.duplicates = 1;
        if (condition == 5) { /* Mixed reception at raw 299. */
            state.seen[43U / 64U] |= UINT64_C(1) << (43U % 64U);
        }
        if (condition == 6) { /* Raw 300 succeeds above a missing raw 299. */
            for (unsigned int seq = 1; seq <= 387U; ++seq)
                if (test_byte_scan_length(seq) == 300U)
                    state.seen[(seq - 1U) / 64U] |= UINT64_C(1) << ((seq - 1U) % 64U);
        }
        if (condition == 7) state.end = false;
        test_sweep_adjacent_log[0] = '\0';
        frm2_sweep_report(&state, condition == 8 ? "receiver-stopped" :
                                  condition == 9 ? "timeout" : "end+grace");
        assert(strstr(test_sweep_boundary_log, "established=0") != NULL);
        assert(test_sweep_adjacent_log[0] == '\0');
        if (condition == 5)
            assert(strstr(test_sweep_boundary_log, "mixed_sizes=1 monotonic=0"));
        if (condition == 6)
            assert(strstr(test_sweep_boundary_log, "monotonic=0"));
    }
}

static void test_byte_scan_receiver(void)
{
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5};
    frm2_sweep_rx_t state = {0};
    uint8_t frame[512];
    for (uint16_t seq = 0; seq <= 388U; ++seq) {
        unsigned int length = frm2_sweep_build(frame, 99, seq, FRM2_SWEEP_BYTE_SCAN);
        assert(frm2_sweep_receive(&state, source, frame, length, 1 + seq * 1000000LL));
        if (seq && seq != 388) assert(frm2_sweep_has(&state, seq));
    }
    assert(state.mode == FRM2_SWEEP_BYTE_SCAN && state.seen[6] == 7U);
    for (unsigned int i = 0; i < 6U; ++i) assert(state.seen[i] == UINT64_MAX);
    frm2_sweep_build(frame, 99, 1, FRM2_SWEEP_COARSE);
    assert(frm2_sweep_receive(&state, source, frame, 256, state.ended));
    assert(state.mode == FRM2_SWEEP_BYTE_SCAN &&
           strstr(test_last_log, "unexpected source/run/mode"));
    frm2_sweep_poll(&state, state.ended + FRM2_SWEEP_GRACE_US);
    assert(state.reported && strstr(test_last_log, "RX PASS"));
    assert(strstr(test_last_log, "verified=387/387"));
    assert(strstr(test_sweep_boundary_log, "established=0 last_full=384 first_missing=-1"));

    memset(&state, 0, sizeof(state));
    frm2_sweep_build(frame, 100, 0, FRM2_SWEEP_BYTE_SCAN);
    assert(frm2_sweep_receive(&state, source, frame, 24, 1));
    frm2_sweep_poll(&state, 1 + FRM2_SWEEP_TIMEOUT_US);
    assert(!state.reported);
    frm2_sweep_poll(&state, FRM2_BYTE_SCAN_TIMEOUT_US);
    assert(!state.reported);
    frm2_sweep_poll(&state, 1 + FRM2_BYTE_SCAN_TIMEOUT_US);
    assert(state.reported && strstr(test_last_log, "RX FAIL"));
    assert(strstr(test_last_log, "verified=0/387") && strstr(test_last_log, "reason=timeout"));
}

static frm2_sweep_rx_t test_tight_mask(unsigned int cutoff, uint64_t run)
{
    frm2_sweep_rx_t state = {.active = true, .start = true, .end = true,
                             .mode = FRM2_SWEEP_TIGHT_SCAN, .run = run};
    uint16_t order[30];
    frm2_tight_order(run, order);
    for (unsigned int i = 0; i < 30U; ++i)
        if (order[i] < cutoff) state.seen[0] |= UINT64_C(1) << i;
    return state;
}

static void test_tight_scan(void)
{
    uint16_t order[30], repeated[30], previous[30] = {0};
    for (uint64_t run = 1; run <= 128U; ++run) {
        unsigned int counts[6] = {0};
        frm2_tight_order(run, order);
        frm2_tight_order(run, repeated);
        assert(memcmp(order, repeated, sizeof(order)) == 0);
        if (run == 2) assert(memcmp(order, previous, sizeof(order)) != 0);
        memcpy(previous, order, sizeof(order));
        for (unsigned int i = 0; i < 30U; ++i) {
            assert(order[i] >= 337 && order[i] <= 342);
            ++counts[order[i] - 337U];
        }
        for (unsigned int i = 0; i < 6U; ++i) assert(counts[i] == 5);
    }
    const uint64_t identity = UINT64_C(0x1020304050607080);
    uint8_t frame[513];
    frm2_tight_order(identity, order);
    assert(frm2_sweep_count(FRM2_SWEEP_TIGHT_SCAN) == 30);
    assert(frm2_sweep_end(FRM2_SWEEP_TIGHT_SCAN) == 31);
    for (uint16_t seq = 0; seq <= 31U; ++seq) {
        unsigned int length = frm2_sweep_build(frame, identity, seq, FRM2_SWEEP_TIGHT_SCAN);
        assert(length == (seq == 0 || seq == 31 ? 24U : order[seq - 1U]));
        assert(frame[7] == '3' && frm2_sweep_candidate(frame, length));
        uint64_t run;
        uint16_t decoded;
        assert(frm2_sweep_validate(frame, length, &run, &decoded) == -1);
        assert(run == identity && decoded == seq);
        for (unsigned int i = 0; i < length; ++i) {
            frame[i] ^= 1;
            assert(frm2_sweep_validate(frame, length, &run, &decoded) >= 0);
            frame[i] ^= 1;
        }
        assert(frm2_sweep_validate(frame, length - 1U, &run, &decoded) >= 0);
        assert(frm2_sweep_validate(frame, length + 1U, &run, &decoded) >= 0);
    }
    assert(frm2_sweep_length(32, FRM2_SWEEP_TIGHT_SCAN, identity) == 0);
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5};
    for (unsigned int cutoff = 337; cutoff <= 343U; ++cutoff) {
        frm2_sweep_rx_t state = {0};
        for (uint16_t seq = 0; seq <= 31U; ++seq) {
            unsigned int length = frm2_sweep_build(frame, identity, seq, FRM2_SWEEP_TIGHT_SCAN);
            if (seq && seq != 31 && length >= cutoff) continue;
            assert(frm2_sweep_receive(&state, source, frame, length, 1 + seq * 1000000LL));
        }
        unsigned int sizes_before = test_sweep_size_logs;
        frm2_sweep_poll(&state, state.ended + FRM2_SWEEP_GRACE_US);
        assert(state.reported && test_sweep_size_logs == sizes_before + 6U);
        if (cutoff == 337 || cutoff == 343) {
            assert(strstr(test_sweep_boundary_log, "established=0"));
        } else {
            char expected[96];
            snprintf(expected, sizeof(expected), "established=1 last_full=%u first_missing=%u",
                     cutoff - 1U, cutoff);
            assert(strstr(test_sweep_boundary_log, expected));
        }
        if (cutoff == 341)
            assert(strstr(test_sweep_adjacent_log, "raw=340/341 vendor=362/363 blocks=6/6"));
        if (cutoff == 343) assert(strstr(test_last_log, "verified=30/30"));
    }
    for (unsigned int condition = 0; condition < 8U; ++condition) {
        frm2_sweep_rx_t state = test_tight_mask(341, identity);
        if (condition == 0) state.start = false;
        if (condition == 1) state.end = false;
        if (condition == 2) state.corrupt = 1;
        if (condition == 3) state.duplicates = 1;
        if (condition == 4 || condition == 5) {
            for (unsigned int i = 0; i < 30U; ++i) {
                if (order[i] == (condition == 4 ? 341U : 342U)) {
                    state.seen[0] |= UINT64_C(1) << i;
                    if (condition == 4) break;
                }
            }
        }
        test_sweep_adjacent_log[0] = '\0';
        frm2_sweep_report(&state, condition == 6 ? "receiver-stopped" :
                                  condition == 7 ? "timeout" : "end+grace");
        assert(strstr(test_sweep_boundary_log, "established=0"));
        assert(test_sweep_adjacent_log[0] == '\0');
        if (condition == 4)
            assert(strstr(test_sweep_boundary_log, "mixed_sizes=1 monotonic=0"));
        if (condition == 5) assert(strstr(test_sweep_boundary_log, "monotonic=0"));
    }
    frm2_sweep_rx_t state = {0};
    frm2_sweep_build(frame, identity, 0, FRM2_SWEEP_TIGHT_SCAN);
    assert(frm2_sweep_receive(&state, source, frame, 24, 1));
    frm2_sweep_build(frame, identity, 0, FRM2_SWEEP_BYTE_SCAN);
    assert(frm2_sweep_receive(&state, source, frame, 24, 2));
    assert(state.mode == FRM2_SWEEP_TIGHT_SCAN &&
           strstr(test_last_log, "unexpected source/run/mode"));
    frm2_sweep_poll(&state, FRM2_SWEEP_TIMEOUT_US);
    assert(!state.reported);
    frm2_sweep_poll(&state, 1 + FRM2_SWEEP_TIMEOUT_US);
    assert(state.reported && strstr(test_last_log, "verified=0/30"));
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
static void test_sweep_sender(frm2_sweep_mode_t mode)
{
    hgic.status.conn_state = 1;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    test_trace_spi = true;
#endif
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5};
    const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
    const uint8_t zero[6] = {0};
    frm2_sweep_tx_t state = {.mode = mode};
    frm2_sweep_transmit(&state, 1);
    assert(test_sweep_writes == 0);
    frm2_sweep_select_peer(&state, broadcast, 1);
    frm2_sweep_select_peer(&state, zero, 1);
    assert(!state.selected);
    frm2_sweep_select_peer(&state, source, 100);
    assert(state.selected && state.run != 0);
    assert(test_random_calls == (mode == FRM2_SWEEP_TIGHT_SCAN ? 2U : 0U));
    if (mode != FRM2_SWEEP_TIGHT_SCAN) assert(state.run == 100);
    uint64_t expected_run = state.run;
    uint16_t order[FRM2_TIGHT_SCAN_COUNT];
    frm2_tight_order(expected_run, order);
    unsigned int tight_counts[6] = {0};
    frm2_sweep_select_peer(&state, broadcast, 200);
    assert(memcmp(state.destination, source, 6) == 0);
    frm2_sweep_rx_t receiver = {0};
    for (uint16_t sequence = 0; sequence <= frm2_sweep_end(mode); ++sequence) {
        unsigned int before = test_sweep_writes;
        frm2_sweep_transmit(&state, state.due - 1);
        assert(test_sweep_writes == before);
        tx_window = 2;
        int64_t now = state.due + (sequence == 20 ? 5000000LL : 0);
        frm2_sweep_transmit(&state, now);
        assert(test_sweep_writes == before + 1);
        unsigned int raw;
        if (mode == FRM2_SWEEP_TIGHT_SCAN) {
            raw = sequence == 0 || sequence == 31 ? 24 : order[sequence - 1U];
            if (sequence && sequence != 31) ++tight_counts[raw - 337U];
        } else {
            raw = mode == FRM2_SWEEP_BYTE_SCAN ? test_byte_scan_length(sequence) :
                  frm2_sweep_length(sequence, FRM2_SWEEP_COARSE, expected_run);
        }
        assert(test_sweep_packet_length == raw + 22U);
        assert(test_sweep_packet[2] == HGIC_HDR_TYPE_FRM2);
        assert(memcmp(test_sweep_packet + 8, source, 6) == 0);
        assert(test_sweep_packet[20] == 0x66 && test_sweep_packet[21] == 0xaa);
        uint64_t run;
        uint16_t decoded;
        assert(frm2_sweep_validate(test_sweep_packet + 22, raw, &run, &decoded) == -1);
        assert(run == expected_run && decoded == sequence);
        put_unaligned_le16(HGIC_HDR_RX_MAGIC, test_sweep_packet);
        memcpy(test_sweep_packet + 14, source, 6);
        robusto_halow_spi_packet_t packet;
        assert(robusto_halow_spi_parse(test_sweep_packet,
                                      (test_sweep_packet_length + 63U) & ~63U, &packet));
        assert(packet.length == raw);
        assert(frm2_sweep_receive(&receiver, packet.source, packet.frame, packet.length, now));
        if (sequence < frm2_sweep_end(mode))
            assert(state.due == now + FRM2_SWEEP_INTERVAL_US);
    }
    if (mode == FRM2_SWEEP_TIGHT_SCAN)
        for (unsigned int i = 0; i < 6U; ++i) assert(tight_counts[i] == 5U);
    unsigned int writes_expected = frm2_sweep_count(mode) + 2U;
    assert(state.done && test_sweep_writes == writes_expected);
    frm2_sweep_transmit(&state, INT64_MAX);
    assert(test_sweep_writes == writes_expected);
    frm2_sweep_poll(&receiver, receiver.ended + FRM2_SWEEP_GRACE_US);
    assert(receiver.reported && strstr(test_last_log, "RX PASS") != NULL);

    for (unsigned int failure = 0; failure < 3; ++failure) {
        memset(&state, 0, sizeof(state));
        state.mode = mode;
        frm2_sweep_select_peer(&state, source, 101 + failure);
        hgic.status.conn_state = failure != 0;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
        ap_interface_ready = failure != 0;
#endif
        tx_window = failure == 1 ? 1 : 2;
        test_sweep_fail_at = failure == 2 ? test_sweep_writes + 1 : 0;
        frm2_sweep_transmit(&state, state.due);
        assert(state.done && state.sequence == 0);
        assert(strstr(test_last_log, "TX FAIL") != NULL);
        unsigned int writes = test_sweep_writes;
        frm2_sweep_transmit(&state, INT64_MAX);
        assert(test_sweep_writes == writes);
    }
    if (mode == FRM2_SWEEP_TIGHT_SCAN) {
        memset(&state, 0, sizeof(state));
        state.mode = mode;
        test_random_zero = true;
        unsigned int before = test_random_calls;
        frm2_sweep_select_peer(&state, source, 200);
        assert(state.done && test_random_calls == before + 2U);
        assert(strstr(test_last_log, "random run identity zero"));
        frm2_sweep_select_peer(&state, source, 201);
        frm2_sweep_transmit(&state, INT64_MAX);
        assert(test_random_calls == before + 2U && state.sequence == 0);
    }
}
#endif

int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    start_fixture();
    test_sweep_wire();
    test_sweep_receiver();
    test_byte_scan_wire();
    test_byte_scan_boundaries();
    test_byte_scan_receiver();
    test_tight_scan();
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_TIGHT_SCAN_TX
    test_sweep_sender(FRM2_SWEEP_TIGHT_SCAN);
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_FRM2_BYTE_SCAN_TX)
    test_sweep_sender(FRM2_SWEEP_BYTE_SCAN);
#else
    test_sweep_sender(FRM2_SWEEP_COARSE);
#endif
#endif
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    start_fixture();
    hgic.status.conn_state = 1;
    test_sweep_pending = 1;
    test_driving_receiver = true;
    test_delay_limit = 3;
    receive_task(NULL);
    test_driving_receiver = false;
    assert(faulted && rx_data_frames == 1 && test_delay_count == 3);
    assert(test_sweep_normal_receives == 0);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    start_fixture();
    hgic.status.conn_state = 1;
    test_sweep_allow_normal = true;
    test_pending_frame = true;
    test_driving_receiver = true;
    test_delay_limit = 3;
    receive_task(NULL);
    test_driving_receiver = false;
    assert(test_sweep_normal_receives == 1);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    puts("FRM2 sweep: coarse/byte compatibility, balanced seeded 337..342 shuffle five each, exact bytes, all boundary/mixed/timeout/error safeguards and paced vendor FRM2 TX passed.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(test_echo_count == 70 && test_echo_pass_logs == 7 && test_data_writes == 0);
    assert(checked_writes == 0);
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_echo_count == 140 && test_echo_pass_logs == 14 && test_resets == 1);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    for (unsigned int failure = 1; failure <= 5; ++failure) {
        clear_fixture();
        test_echo_fail_at = 35;
        test_echo_failure = failure;
        test_echo_corrupt_offset = UINT32_MAX;
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_echo_count == 35 && test_echo_pass_logs == 3 && test_resets == 0);
        assert(test_link_ups == 0 && test_data_writes == 0);
        assert_stopped();
    }
    const unsigned int offsets[] = {0, 4, 263, 400};
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        clear_fixture();
        test_echo_fail_at = 52;
        test_echo_failure = 1;
        test_echo_corrupt_offset = offsets[i];
        assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
        assert(test_echo_count == 52 && test_echo_pass_logs == 5);
        assert_stopped();
    }
    clear_fixture();
    echo_serial = UINT32_MAX;
    assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
    assert(test_echo_count == 0 && test_echo_pass_logs == 0);
    assert_stopped();
    puts("SPI local echo: 70 byte-exact padded trials, 490/491 boundary, reset reapplication, corruption/identity/length/timeout/submission/RX failure gating, no RF writes and identity exhaustion passed.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    start_fixture();
    assert(checked_writes == 0 && test_write_check_logs == 0);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    const uint8_t destination[6] = {2, 1, 2, 3, 4, 5};
    uint8_t frame[ROBUSTO_HALOW_FRAME_MAX] = {0};
    const uint32_t lengths[] = {1, 256, 384, 490, 491, 512, 1024, ROBUSTO_HALOW_FRAME_MAX};
    for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        trace_fixture();
        tx_window = 2;
        assert(backend_write(NULL, destination, frame, lengths[i]) == (int)lengths[i]);
        assert(write_check.flags == 0 && !write_check.active && !faulted);
        assert(strstr(test_last_log, "SDSPI_CHECK write=1") != NULL);
        finish_trace_fixture();
    }
    trace_fixture();
    for (unsigned int i = 0; i < 130; ++i) {
        tx_window = 2;
        assert(backend_write(NULL, destination, frame, 512) == 512);
    }
    assert(checked_writes == 130 && test_write_check_logs == 6);
    finish_trace_fixture();
    for (unsigned int failure = 0; failure < 5; ++failure) {
        trace_fixture();
        tx_window = 2;
        if (failure == 0) test_trace_response = 0xeb;
        if (failure == 1) test_trace_response = 0xff;
        if (failure == 2) test_trace_spi_failure = true;
        if (failure == 3) test_trace_short_write = true;
        if (failure == 4) test_check_busy_stuck = true;
        assert(backend_write(NULL, destination, frame, 512) == ROB_ERR_SEND_FAIL);
        assert(write_check.flags != 0 && !write_check.active && faulted);
        unsigned int writes = test_data_writes;
        assert(backend_write(NULL, destination, frame, 512) == ROB_ERR_NOT_READY);
        assert(test_data_writes == writes);
        finish_trace_fixture();
    }
    puts("SDSPI backend checks: actual SPI callback wiring, size boundaries, explicit failure/fault gating and no retry passed.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    const unsigned int sequence[] = {HGIC_CMD_RADIO_ONOFF,
        HGIC_CMD_GET_FW_INFO, HGIC_CMD_SET_WORK_MODE, HGIC_CMD_RADIO_ONOFF,
        TEST_STATUS_COMMAND};
    size_t count = sizeof(sequence) / sizeof(sequence[0]);
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(test_command_count == count && memcmp(test_commands, sequence, sizeof(sequence)) == 0);
    test_command_count = 0;
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_command_count == count + 1 && test_commands[0] == HGIC_CMD_RESET);
    assert(memcmp(test_commands + 1, sequence, sizeof(sequence)) == 0);
    assert(robusto_halow_stop() == ROB_OK);
    assert_stopped();
    for (size_t stage = 0; stage < count - 3; ++stage) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            clear_fixture();
            if (failure == 0) test_reject_command = sequence[stage];
            if (failure == 1) test_drop_command = sequence[stage];
            if (failure == 2) test_submit_failure = sequence[stage];
            assert(robusto_halow_start() == ROB_ERR_INIT_FAIL);
            assert(test_command_count == stage + 1 &&
                   test_commands[stage] == sequence[stage]);
            assert(test_radio_enable_logs == 0 && test_probe_count == 0 && test_resets == 0);
            assert_stopped();
        }
    }
    puts("Startup radio-off: first command 65/zero, firmware-info ordering, reset reapplication and failure gating passed.");
    return 0;
}
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
int main(void)
{
    assert(robusto_halow_spi_register() == ROB_OK);
    test_connection_event_diagnostics();
    test_rx_cookie_diagnostics();
    test_all_event_diagnostics();
    test_tx_credit_backpressure();
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    test_fragment_tx_trace();
#endif
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
    test_rx_cookie_diagnostics();
    test_all_event_diagnostics();
    test_tx_credit_backpressure();
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
    test_rx_cookie_diagnostics();
    test_all_event_diagnostics();
    test_tx_credit_backpressure();
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    test_fragment_tx_trace();
#endif
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
