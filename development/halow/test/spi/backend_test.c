#include "backend_test_stubs.h"
#include "halow_spi.c"
#include <stdio.h>

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
    if (command_id == HGIC_CMD_RESET) {
        assert(stopping && rx_task == NULL && !test_link_ready);
        ++test_resets;
        if (!test_reset_fails && test_reset_clears_failure) {
            test_drop_command = test_reject_command = 0;
        }
        if (command_id == HGIC_CMD_SET_WORK_MODE) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
            assert(memcmp(data + CMD_HDR_LEN, "ap", 3) == 0);
#else
            assert(memcmp(data + CMD_HDR_LEN, "sta", 4) == 0);
#endif
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
        put_unaligned_le16(CMD_HDR_LEN + 6, data + 4);
        test_pending_event = 0;
        return CMD_HDR_LEN + 6;
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
    test_probe_count = test_link_ups = test_delay_count = test_delay_limit = 0;
    test_ticks = 0;
}

static void start_fixture(void)
{
    clear_fixture();
    assert(robusto_halow_start() == ROB_OK);
    assert(test_init_calls == 1 && test_command_count == 3);
    assert(test_commands[0] == 43 && test_commands[1] == 41 && test_commands[2] == TEST_STATUS_COMMAND);
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

int main(void)
{
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    assert(robusto_halow_spi_register() == ROB_OK);
    assert(test_driver.reset_and_reinitialize != NULL);
    assert(reset_and_reinitialize() == ESP_ERR_INVALID_STATE);
    assert(test_init_calls == 0 && test_resets == 0);

    clear_fixture();
    assert(reset_and_reinitialize() == ESP_OK);
    assert(test_init_calls == 2 && test_resets == 1 && test_starts == 1 && test_stops == 0);
    assert(test_command_count == 4 && test_commands[0] == 132 &&
           test_commands[1] == 43 && test_commands[2] == 41 && test_commands[3] == TEST_STATUS_COMMAND);
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
    assert(test_resets == 1 && test_command_count == 4 && test_commands[0] == 132);
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
    assert(test_command_count == 5 && test_commands[1] == 132 &&
           test_commands[2] == 43 && test_commands[3] == 41 && test_commands[4] == TEST_STATUS_COMMAND);
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
    assert(test_init_calls == 2 && test_command_count == 7);
    assert(test_commands[3] == 132 && test_commands[4] == 43 &&
           test_commands[5] == 41 && test_commands[6] == TEST_STATUS_COMMAND);
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
    assert(test_command_count == 5 && test_commands[4] == 43);
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
    assert(test_command_count == 3 && test_probe_count == 0);
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
