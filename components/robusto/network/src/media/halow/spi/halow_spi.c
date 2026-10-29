#include "sdkconfig.h"
#ifdef CONFIG_ROBUSTO_HALOW_SPI

#include "halow_spi.h"
#include "halow_spi_wire.h"
#include "halow_spi_trace.h"
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
#include "halow_frm2_sweep.h"
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
#include "esp_random.h"
#endif
#if defined(CONFIG_ROBUSTO_HALOW_SPI_FRM2_BYTE_SCAN_TX) && defined(CONFIG_ROBUSTO_HALOW_SPI_FRM2_TIGHT_SCAN_TX)
#error "Select only one FRM2 sweep pattern"
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
#include "halow_sdspi_check.h"
#endif
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_memory_utils.h"
#include "esp_private/spi_common_internal.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "hal/spi_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hgic_raw.h"
#include "hgic_sdspi.h"
#include "robusto_halow.h"
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
#include "robusto_message_def.h"
#endif
#include <string.h>

#if defined(CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN) && CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN >= 0
#define HALOW_CONFIGURE_MINIMUM_MCS
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "The Huge-IC HaLow SPI backend currently supports ESP32-P4 only"
#endif

#define HALOW_SCLK CONFIG_ROBUSTO_HALOW_SPI_SCLK_GPIO
#define HALOW_MOSI CONFIG_ROBUSTO_HALOW_SPI_MOSI_GPIO
#define HALOW_MISO CONFIG_ROBUSTO_HALOW_SPI_MISO_GPIO
#define HALOW_CS CONFIG_ROBUSTO_HALOW_SPI_CS_GPIO
#define HALOW_IRQ CONFIG_ROBUSTO_HALOW_SPI_IRQ_GPIO
#define HALOW_DEBUG_RX CONFIG_ROBUSTO_HALOW_SPI_DEBUG_RX_GPIO
#define HALOW_DEBUG_UART UART_NUM_2
#define HALOW_DEBUG_CAPTURE_MAX 2048U
#define HALOW_DIAGNOSTIC_SKIP_FW_INFO 0
#ifdef CONFIG_ROBUSTO_HALOW_SPI_DIAGNOSTICS
#define HALOW_DIAGNOSTICS_ENABLED 1
#else
#define HALOW_DIAGNOSTICS_ENABLED 0
#endif
#define COMMAND_TIMEOUT_MS 2000
#define STATUS_INTERVAL_MS 1000
#define STATUS_TIMEOUT_MS 5000
#define LOCK_TIMEOUT_MS 1000

#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
#define HALOW_STATUS_COMMAND HGIC_CMD_GET_MODE
#define HALOW_ROLE_NAME "AP"
#else
#define HALOW_STATUS_COMMAND HGIC_CMD_GET_CONN_STATE
#define HALOW_ROLE_NAME "STA"
#endif

_Static_assert(ROBUSTO_HALOW_FRAME_MAX == ROBUSTO_HALOW_SPI_FRAME_MAX,
               "Robusto and physical backend frame limits differ");

extern struct hgic_raw hgic;
extern int hgic_sdspi_get_devid(void *priv, unsigned short *vendor_id,
                              unsigned short *device_id);
static const char *TAG = "robusto_halow_spi";
static SemaphoreHandle_t lock;
static SemaphoreHandle_t exited;
static spi_device_handle_t device;
static TaskHandle_t rx_task;
static bool bus_initialized;
static bool cs_initialized;
static bool irq_initialized;
static bool running;
static bool stopping;
static bool faulted;
static bool ap_interface_ready;
static uint16_t tx_window;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
static robusto_sdspi_check_t write_check;
static uint32_t checked_writes, checked_block_sizes;

static void log_write_check(int sent, bool valid, int64_t elapsed_us)
{
    unsigned int blocks = write_check.blocks < ROBUSTO_SDSPI_CHECK_BLOCKS
                              ? write_check.blocks : ROBUSTO_SDSPI_CHECK_BLOCKS;
    char responses[ROBUSTO_SDSPI_CHECK_BLOCKS * 2U + 1U];
    char busy[ROBUSTO_SDSPI_CHECK_BLOCKS * 2U + 1U];
    static const char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < blocks; ++i) {
        responses[i * 2U] = hex[write_check.response[i] >> 4];
        responses[i * 2U + 1U] = hex[write_check.response[i] & 15U];
        busy[i * 2U] = hex[write_check.busy[i] >> 4];
        busy[i * 2U + 1U] = hex[write_check.busy[i] & 15U];
    }
    responses[blocks * 2U] = busy[blocks * 2U] = '\0';
    ESP_LOG_LEVEL_LOCAL(valid ? ESP_LOG_INFO : ESP_LOG_ERROR, TAG,
        "SDSPI_CHECK write=%lu vendor_bytes=%lu blocks=%lu/%lu declared=%lu commands=%lu stops=%lu"
        " tokens=%s busy=%s polls=%lu flags=0x%02lx first_bad=%ld vendor_rc=%d elapsed_us=%lld",
        (unsigned long)checked_writes, (unsigned long)write_check.bytes,
        (unsigned long)write_check.blocks, (unsigned long)write_check.expected_blocks,
        (unsigned long)write_check.declared_blocks, (unsigned long)write_check.commands,
        (unsigned long)write_check.stops, responses, busy, (unsigned long)write_check.busy_reads,
        (unsigned long)write_check.flags,
        write_check.first_bad_block == UINT32_MAX ? -1L : (long)write_check.first_bad_block,
        sent, (long long)elapsed_us);
}
#endif
static bool window_pending;
static TickType_t window_requested;
static TickType_t last_interface_check;
static esp_err_t transaction_error;
static uint8_t rx_buffer[ROBUSTO_HALOW_SPI_RX_CAPACITY];
static uint8_t delivery_buffer[ROBUSTO_HALOW_SPI_FRAME_MAX];
static DMA_ATTR uint8_t spi_tx[ROBUSTO_HALOW_SPI_RX_CAPACITY];
static DMA_ATTR uint8_t spi_rx[ROBUSTO_HALOW_SPI_RX_CAPACITY];
static bool trace_initialization;
static bool trace_control_submission;
static unsigned int rejected_header_logs;
static uint32_t rx_data_frames;
static uint32_t rx_unready_frames;
#ifdef CONFIG_ROBUSTO_HALOW_RX_TRACE
static uint32_t rx_trace_reads;
#endif
static bool debug_uart_installed;
static QueueHandle_t debug_uart_events;
static unsigned int debug_uart_captured;
static struct {
    uint8_t tx[29];
    uint8_t rx[29];
    unsigned int length;
} init_commands[2];
static struct {
    uint8_t tx[18];
    uint8_t rx[18];
} init_registers[32];
static unsigned int init_register_count;
static bool init_register_overflow;
typedef struct {
    uint8_t tx[69];
    uint8_t rx[69];
    unsigned int length;
    uint8_t dma_tx[69];
    uintptr_t dma_address;
    unsigned int dma_length;
    unsigned int dma_size;
    unsigned int spi_bits;
    bool dma_captured;
    bool dma_single;
    bool tx_lsb_first;
} control_trace_t;
static control_trace_t control_trace[2];
static spi_dma_desc_t *tx_descriptor;
static bool control_buffer_changed;
static unsigned int bitrate_logs;
static unsigned int signal_logs;
static sequence_trace_t sequence_trace[SEQUENCE_TRACE_MAX];
static unsigned int sequence_count;
static unsigned int sequence_dropped;
static int64_t sequence_begin_us;
typedef struct {
    control_trace_t *control;
    sequence_trace_t *sequence;
} transfer_capture_t;

static uint8_t IRAM_ATTR cs_output_latch(void)
{
    const unsigned int pin = HALOW_CS;
    uint32_t latch = pin < 32U ? GPIO_LL_GET_HW(0)->out.val
                              : GPIO_LL_GET_HW(0)->out1.val;
    return (latch >> (pin % 32U)) & 1U;
}

static sequence_trace_t *begin_sequence(char operation, unsigned int length)
{
    if (!trace_control_submission) {
        return NULL;
    }
    if (sequence_count == SEQUENCE_TRACE_MAX) {
        ++sequence_dropped;
        return NULL;
    }
    sequence_trace_t *entry = &sequence_trace[sequence_count++];
    memset(entry, 0, sizeof(*entry));
    entry->operation = operation;
    entry->length = length;
    entry->repetitions = 1;
    entry->begin_us = esp_timer_get_time();
    entry->cs_begin = cs_output_latch();
    return entry;
}

static void finish_sequence(sequence_trace_t *entry, esp_err_t result)
{
    if (entry == NULL) {
        return;
    }
    entry->end_us = esp_timer_get_time();
    entry->cs_end = cs_output_latch();
    entry->result = result;
    entry->last_begin_us = entry->begin_us;
    entry->last_pre_us = entry->pre_us;
    entry->last_post_us = entry->post_us;
    if (sequence_count >= 2U &&
        robusto_halow_spi_compact_read(&sequence_trace[sequence_count - 2U], entry)) {
        --sequence_count;
    }
}

static bool take_lock(void)
{
    return xSemaphoreTake(lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
#define TX_TRACE_FRAGMENTS 64U
#define TX_TRACE_BLOCKS 26U
typedef struct {
    uint32_t calls, busy, sequence, first_sequence, frame_bytes, index;
    uint32_t spi_calls, spi_failures, commands, blocks, block_overflow;
    uint32_t stops, busy_reads, busy_zero, busy_nonzero;
    uint16_t window_before, window_after;
    uint8_t destination[6], command_response[10];
    uint8_t block_response[TX_TRACE_BLOCKS], block_busy[TX_TRACE_BLOCKS];
    unsigned int declared_blocks, vendor_bytes, raw_bytes, header_bytes, cookie, ifidx;
    int vendor_result, sdspi_result, backend_result;
    esp_err_t spi_result;
    bool vendor_called, raw_called, wire_header_seen, wire_header_match;
} tx_trace_record_t;
static struct {
    bool armed, dumped;
    uint32_t hash, fragment_count, unrecorded_calls, request_crc;
    uint8_t version, transfer_id[8];
    tx_trace_record_t request, fragments[TX_TRACE_FRAGMENTS];
} tx_trace;
static tx_trace_record_t *tx_trace_current;

static uint32_t tx_trace_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static tx_trace_record_t *tx_trace_frame(const uint8_t destination[6],
                                         const uint8_t *frame, uint32_t length)
{
    if (tx_trace.dumped || length < ROBUSTO_HALOW_HEADER_SIZE + 6U ||
        frame[0] != 'R' || frame[1] != 'H' || frame[2] != 1U ||
        frame[3] > 1U) return NULL;
    const uint8_t *payload = frame + ROBUSTO_HALOW_HEADER_SIZE;
    if ((payload[4] & 7U) != MSG_FRAGMENTED) return NULL;
    uint32_t extension = (payload[5] & 0x80U) ? 12U : 0U;
    uint8_t subtype = payload[5] & 0x7fU;
    tx_trace_record_t *record = NULL;
    if (subtype == FRAG_REQUEST && length == ROBUSTO_HALOW_HEADER_SIZE + 22U + extension) {
        uint32_t hash = tx_trace_u32(payload + 18U + extension);
        if (!tx_trace.armed) {
            tx_trace.armed = true;
            tx_trace.hash = hash;
            tx_trace.fragment_count = tx_trace_u32(payload + 10U + extension);
            tx_trace.request_crc = tx_trace_u32(payload);
            tx_trace.version = payload[5] & 0x80U;
            if (extension) memcpy(tx_trace.transfer_id, payload + 6, 8);
        }
        if (hash == tx_trace.hash) {
            record = &tx_trace.request;
            record->index = UINT32_MAX;
        }
    } else if (subtype == FRAG_MESSAGE &&
               length >= ROBUSTO_HALOW_HEADER_SIZE + 10U + extension && tx_trace.armed &&
               tx_trace_u32(payload) == tx_trace.hash) {
        uint32_t index = tx_trace_u32(payload + 6U + extension);
        if (index < TX_TRACE_FRAGMENTS && index < tx_trace.fragment_count) {
            record = &tx_trace.fragments[index];
            record->index = index;
        } else {
            ++tx_trace.unrecorded_calls;
        }
    }
    if (record != NULL) {
        record->sequence = tx_trace_u32(frame + 4);
        if (record->calls++ == 0) record->first_sequence = record->sequence;
        record->frame_bytes = length;
        record->window_before = tx_window;
        record->vendor_result = ROB_ERR_NOT_READY;
        record->sdspi_result = ROB_ERR_NOT_READY;
        record->backend_result = ROB_ERR_NOT_READY;
        memcpy(record->destination, destination, sizeof(record->destination));
    }
    return record;
}

static void tx_trace_transfer(const uint8_t *tx, const uint8_t *rx,
                              unsigned int length, esp_err_t result)
{
    tx_trace_record_t *record = tx_trace_current;
    if (record == NULL) return;
    ++record->spi_calls;
    if (result != ESP_OK) {
        if (record->spi_failures++ == 0) record->spi_result = result;
        return;
    }
    if (tx != NULL && rx != NULL && length == 17U &&
        tx[0] == 0xff && tx[1] == 0x75 && (tx[2] & 0x80U)) {
        ++record->commands;
        record->declared_blocks = (((unsigned int)tx[4] << 8) | tx[5]) & 0x1ffU;
        if (record->declared_blocks == 0) record->declared_blocks = 512U;
        memcpy(record->command_response, rx + 7, sizeof(record->command_response));
    } else if (tx != NULL && rx != NULL && length == 69U && tx[0] == 0xfc) {
        if (record->blocks == 0) {
            const uint8_t *wire = tx + 1U + 22U;
            const uint8_t *payload = wire + ROBUSTO_HALOW_HEADER_SIZE;
            bool request = record->index == UINT32_MAX;
            uint32_t extension = tx_trace.version ? 12U : 0U;
            record->wire_header_seen = true;
            record->wire_header_match =
                wire[0] == 'R' && wire[1] == 'H' && wire[2] == 1U &&
                wire[3] == (request ? 1U : 0U) &&
                memcmp(tx + 1U + sizeof(struct hgic_frm_hdr2),
                       record->destination, sizeof(record->destination)) == 0 &&
                tx_trace_u32(wire + 4) == record->sequence &&
                ((unsigned int)wire[14] | ((unsigned int)wire[15] << 8)) ==
                    record->frame_bytes - ROBUSTO_HALOW_HEADER_SIZE &&
                (payload[4] & 7U) == MSG_FRAGMENTED &&
                payload[5] == (tx_trace.version | (request ? FRAG_REQUEST : FRAG_MESSAGE)) &&
                (!extension || memcmp(payload + 6, tx_trace.transfer_id, 8) == 0) &&
                tx_trace_u32(payload) == (request ? tx_trace.request_crc : tx_trace.hash) &&
                (request || tx_trace_u32(payload + 6U + extension) == record->index);
        }
        if (record->blocks < TX_TRACE_BLOCKS) {
            record->block_response[record->blocks] = rx[67];
            record->block_busy[record->blocks] = rx[68];
        } else {
            ++record->block_overflow;
        }
        ++record->blocks;
    } else if (tx != NULL && length == 1U && tx[0] == 0xfd) {
        ++record->stops;
    } else if (tx == NULL && rx != NULL) {
        ++record->busy_reads;
        for (unsigned int i = 0; i < length; ++i) {
            if (rx[i] == 0) ++record->busy_zero;
            else ++record->busy_nonzero;
        }
    }
}

static void tx_trace_hex(char *out, const uint8_t *data, unsigned int length)
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < length; ++i) {
        out[i * 2] = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 15U];
    }
    out[length * 2] = '\0';
}

static void tx_trace_dump_record(const char *kind, unsigned int index,
                                  const tx_trace_record_t *record)
{
    if (record->calls == 0) return;
    char command[21], responses[TX_TRACE_BLOCKS * 2 + 1], busy[TX_TRACE_BLOCKS * 2 + 1];
    unsigned int blocks = record->blocks < TX_TRACE_BLOCKS ? record->blocks : TX_TRACE_BLOCKS;
    tx_trace_hex(command, record->command_response, record->commands ? 10U : 0U);
    tx_trace_hex(responses, record->block_response, blocks);
    tx_trace_hex(busy, record->block_busy, blocks);
    ESP_LOGI(TAG, "TXTRACE %s hash=%lu index=%u seq=%lu first_seq=%lu calls=%lu busy=%lu dst=%02x:%02x:%02x:%02x:%02x:%02x"
             " frame=%lu vendor=%u called=%u raw_called=%u raw_bytes=%u hdr_bytes=%u cookie=%u ifidx=%u"
             " sdspi_rc=%d raw_rc=%d backend_rc=%d credits=%u->%u SPI=%s",
             kind, (unsigned long)tx_trace.hash, index,
             (unsigned long)record->sequence, (unsigned long)record->first_sequence,
             (unsigned long)record->calls, (unsigned long)record->busy,
             (unsigned)record->destination[0], (unsigned)record->destination[1],
             (unsigned)record->destination[2], (unsigned)record->destination[3],
             (unsigned)record->destination[4], (unsigned)record->destination[5],
             (unsigned long)record->frame_bytes,
             record->vendor_bytes, record->vendor_called, record->raw_called,
             record->raw_bytes, record->header_bytes, record->cookie, record->ifidx,
             record->sdspi_result, record->vendor_result,
             record->backend_result, (unsigned)record->window_before,
             (unsigned)record->window_after, esp_err_to_name(record->spi_result));
    ESP_LOGI(TAG, "TXTRACE SPI hash=%lu index=%u calls=%lu errors=%lu cmd53=%lu declared_blocks=%u"
             " response=%s blocks=%lu overflow=%lu wire_seen=%u wire_match=%u data_response=%s data_busy=%s stop=%lu"
             " busy_reads=%lu zero=%lu nonzero=%lu; raw responses, RF unconfirmed",
             (unsigned long)tx_trace.hash, index,
             (unsigned long)record->spi_calls, (unsigned long)record->spi_failures,
             (unsigned long)record->commands, record->declared_blocks, command,
             (unsigned long)record->blocks, (unsigned long)record->block_overflow,
             record->wire_header_seen, record->wire_header_match,
             responses, busy, (unsigned long)record->stops,
             (unsigned long)record->busy_reads, (unsigned long)record->busy_zero,
             (unsigned long)record->busy_nonzero);
}

void robusto_halow_spi_dump_tx_trace(void)
{
    if (!take_lock()) {
        ESP_LOGE(TAG, "TXTRACE dump mutex timed out");
        return;
    }
    bool dump = tx_trace.armed && !tx_trace.dumped;
    if (dump) tx_trace.dumped = true;
    xSemaphoreGive(lock);
    if (!dump) return;
    unsigned int seen = 0;
    for (unsigned int i = 0; i < TX_TRACE_FRAGMENTS; ++i) {
        if (tx_trace.fragments[i].calls != 0) ++seen;
    }
    ESP_LOGI(TAG, "TXTRACE first pass hash=%lu declared=%lu recorded=%u capacity=%u truncated=%u unrecorded_calls=%lu;"
             " capture frozen until host reboot; module acceptance/RF unconfirmed",
             (unsigned long)tx_trace.hash, (unsigned long)tx_trace.fragment_count,
             seen, TX_TRACE_FRAGMENTS, tx_trace.fragment_count > TX_TRACE_FRAGMENTS,
             (unsigned long)tx_trace.unrecorded_calls);
    tx_trace_dump_record("request", UINT32_MAX, &tx_trace.request);
    for (unsigned int i = 0; i < TX_TRACE_FRAGMENTS; ++i) {
        tx_trace_dump_record("fragment", i, &tx_trace.fragments[i]);
    }
}
#endif

/* P4/IDF-specific diagnostic: pre_cb runs after DMA setup, before SPI starts.
 * Read uncached aliases so this observes backing memory, not the CPU TX cache.
 * Do not log, allocate, or change the descriptor/buffer in this callback. */
static void IRAM_ATTR capture_dma_submission(spi_transaction_t *transaction)
{
    transfer_capture_t *capture = transaction->user;
    if (capture == NULL) {
        return;
    }
    spi_dev_t *hw = SPI_LL_GET_HW(SPI2_HOST);
    if (capture->sequence != NULL) {
        sequence_trace_t *entry = capture->sequence;
        entry->pre_us = esp_timer_get_time();
        entry->cs_pre = cs_output_latch();
        entry->spi_user = hw->user.val;
        entry->spi_misc = hw->misc.val;
        entry->spi_bits = hw->ms_dlen.ms_data_bitlen + 1;
        entry->pre_captured = true;
    }
    control_trace_t *trace = capture->control;
    if (trace == NULL || tx_descriptor == NULL) {
        return;
    }
    volatile spi_dma_desc_t *descriptor = tx_descriptor;
    trace->dma_address = (uintptr_t)descriptor->buffer;
    trace->dma_length = descriptor->dw0.length;
    trace->dma_size = descriptor->dw0.size;
    trace->dma_single = descriptor->next == NULL && descriptor->dw0.suc_eof;
    trace->spi_bits = hw->ms_dlen.ms_data_bitlen + 1;
    trace->tx_lsb_first = hw->ctrl.wr_bit_order != 0;
    if (!esp_ptr_in_dram((void *)trace->dma_address) ||
        trace->dma_length != trace->length || !trace->dma_single) {
        return;
    }
    const volatile uint8_t *data =
        (const volatile uint8_t *)CACHE_LL_L2MEM_NON_CACHE_ADDR(trace->dma_address);
    for (unsigned int i = 0; i < trace->length; ++i) {
        trace->dma_tx[i] = data[i];
    }
    trace->dma_captured = true;
}

static void IRAM_ATTR capture_spi_completion(spi_transaction_t *transaction)
{
    transfer_capture_t *capture = transaction->user;
    if (capture != NULL && capture->sequence != NULL) {
        capture->sequence->post_us = esp_timer_get_time();
        capture->sequence->cs_post = cs_output_latch();
        capture->sequence->post_captured = true;
    }
}

static void log_submission_sequence(uint16_t id)
{
    ESP_LOGI(TAG, "Command %u sequence: %u entries, %u omitted; times are us from send entry; +/- are vendor markers; CS_latch is not peripheral CS",
             id, sequence_count, sequence_dropped);
    for (unsigned int i = 0; i < sequence_count; ++i) {
        const sequence_trace_t *entry = &sequence_trace[i];
        ESP_LOGI(TAG, "SEQ%u %c len=%u count=%u begin=%lld end=%lld CS_latch=%u->%u result=%s",
                 i, entry->operation, entry->length, entry->repetitions,
                 (long long)(entry->begin_us - sequence_begin_us),
                 (long long)(entry->end_us - sequence_begin_us),
                 entry->cs_begin, entry->cs_end, esp_err_to_name(entry->result));
        if (entry->operation == '+' || entry->operation == '-') {
            continue;
        }
        if (entry->pre_captured && entry->post_captured) {
            spi_user_reg_t user = {.val = entry->spi_user};
            spi_misc_reg_t misc = {.val = entry->spi_misc};
            ESP_LOGI(TAG, "SEQ%u SPI pre=%lld post=%lld CS_latch=%u->%u bits=%u mode_edges=%u/%u duplex=%u phases CMD/ADDR/DUMMY/TX/RX=%u/%u/%u/%u/%u",
                     i, (long long)(entry->pre_us - sequence_begin_us),
                     (long long)(entry->post_us - sequence_begin_us),
                     entry->cs_pre, entry->cs_post, entry->spi_bits,
                     misc.ck_idle_edge, user.ck_out_edge,
                     user.doutdin, user.usr_command,
                     user.usr_addr, user.usr_dummy,
                     user.usr_mosi, user.usr_miso);
            ESP_LOGI(TAG, "SEQ%u hardware CS0 enabled=%u keep_active=%u setup=%u hold=%u",
                     i, !misc.cs0_dis, misc.cs_keep_active, user.cs_setup, user.cs_hold);
            if (entry->repetitions > 1U) {
                ESP_LOGI(TAG, "SEQ%u identical read group: last begin=%lld pre=%lld post=%lld; each transfer is %u clocks",
                         i, (long long)(entry->last_begin_us - sequence_begin_us),
                         (long long)(entry->last_pre_us - sequence_begin_us),
                         (long long)(entry->last_post_us - sequence_begin_us),
                         entry->spi_bits);
            }
        } else {
            ESP_LOGE(TAG, "SEQ%u incomplete SPI callbacks: pre=%d post=%d",
                     i, entry->pre_captured, entry->post_captured);
        }
        unsigned int captured = entry->length < SEQUENCE_BYTES_MAX
                                    ? entry->length : SEQUENCE_BYTES_MAX;
        ESP_LOGI(TAG, "SEQ%u MOSI (%u/%u bytes):", i, captured, entry->length);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, entry->tx, captured, ESP_LOG_INFO);
        if (entry->has_rx && entry->result == ESP_OK) {
            ESP_LOGI(TAG, "SEQ%u MISO (%u/%u bytes):", i, captured, entry->length);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, entry->rx, captured, ESP_LOG_INFO);
        } else {
            ESP_LOGI(TAG, "SEQ%u MISO not captured%s", i,
                     entry->has_rx ? " (transfer failed)" : " (write-only callback)");
        }
    }
    if (sequence_dropped != 0) {
        ESP_LOGW(TAG, "Command %u sequence capture truncated; not a complete sequence", id);
    }
}

static esp_err_t start_debug_capture(void)
{
    if (uart_is_driver_installed(HALOW_DEBUG_UART)) {
        ESP_LOGE(TAG, "Cannot capture HaLow UART: UART%d already in use", HALOW_DEBUG_UART);
        return ESP_ERR_INVALID_STATE;
    }
    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_param_config(HALOW_DEBUG_UART, &config);
    if (result == ESP_OK) {
        result = uart_set_pin(HALOW_DEBUG_UART, UART_PIN_NO_CHANGE, HALOW_DEBUG_RX,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (result == ESP_OK) {
        result = uart_driver_install(HALOW_DEBUG_UART, 4096, 0, 20, &debug_uart_events, 0);
        debug_uart_installed = result == ESP_OK;
    }
    if (result == ESP_OK) {
        debug_uart_captured = 0;
        ESP_LOGI(TAG, "Startup HaLow UART capture: RX GPIO%d at 115200; no UART transmit",
                 HALOW_DEBUG_RX);
    } else {
        ESP_LOGE(TAG, "HaLow UART capture setup failed: %s", esp_err_to_name(result));
    }
    return result;
}

static void capture_debug_uart(void)
{
    if (!debug_uart_installed) {
        return;
    }
    uart_event_t event;
    for (unsigned int i = 0; i < 20U &&
         xQueueReceive(debug_uart_events, &event, 0) == pdTRUE; ++i) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
            event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR ||
            event.type == UART_BREAK) {
            ESP_LOGE(TAG, "HaLow UART capture event=%d; capture may be incomplete or invalid",
                     event.type);
        }
    }
    while (debug_uart_captured < HALOW_DEBUG_CAPTURE_MAX) {
        uint8_t buffer[128];
        unsigned int available = HALOW_DEBUG_CAPTURE_MAX - debug_uart_captured;
        unsigned int count = available < sizeof(buffer) ? available : sizeof(buffer);
        int received = uart_read_bytes(HALOW_DEBUG_UART, buffer, count, 0);
        if (received < 0) {
            ESP_LOGE(TAG, "HaLow UART capture read failed: %d", received);
            return;
        }
        if (received == 0) {
            return;
        }
        debug_uart_captured += (unsigned int)received;
        ESP_LOGI(TAG, "HaLow module UART RX (%d bytes):", received);
        ESP_LOG_BUFFER_HEXDUMP(TAG, buffer, received, ESP_LOG_INFO);
    }
}

static esp_err_t stop_debug_capture(void)
{
    if (!debug_uart_installed) {
        return ESP_OK;
    }
    capture_debug_uart();
    ESP_LOGI(TAG, "HaLow UART capture ended: %u bytes%s", debug_uart_captured,
             debug_uart_captured == HALOW_DEBUG_CAPTURE_MAX ? " (capture limit reached)" : "");
    esp_err_t result = uart_driver_delete(HALOW_DEBUG_UART);
    if (result == ESP_OK) {
        debug_uart_installed = false;
        debug_uart_events = NULL;
    } else {
        ESP_LOGE(TAG, "HaLow UART capture cleanup failed: %s", esp_err_to_name(result));
    }
    return result;
}

/* The upstream callbacks are void. Latch the FIRST failure for the entire
 * enclosing vendor call, including any recovery it attempts internally. */
static void transfer(const uint8_t *tx, uint8_t *rx, unsigned int length)
{
    if (length == 0) {
        return;
    }
    sequence_trace_t *sequence = begin_sequence(tx == NULL ? 'R' : (rx == NULL ? 'W' : 'X'),
                                                length);
    if (transaction_error == ESP_OK &&
        (device == NULL || length > sizeof(spi_tx))) {
        transaction_error = ESP_ERR_INVALID_SIZE;
    }
    if (transaction_error == ESP_OK) {
        if (tx != NULL) {
            memcpy(spi_tx, tx, length);
        } else {
            memset(spi_tx, 0xff, length);
        }
        bool trace_block = trace_control_submission && tx != NULL &&
                           length == 69U && spi_tx[0] == 0xfc;
        if (trace_block) {
            memcpy(control_trace[1].tx, spi_tx, length);
            control_trace[1].length = length;
        }
        bool trace_cmd53 = trace_control_submission && tx != NULL && length == 17U &&
                           spi_tx[0] == 0xff && spi_tx[1] == 0x75 &&
                           (spi_tx[2] & 0x80);
        control_trace_t *trace = trace_block ? &control_trace[1]
                                            : (trace_cmd53 ? &control_trace[0] : NULL);
        if (trace_cmd53) {
            memcpy(trace->tx, spi_tx, length);
            trace->length = length;
        }
        if (sequence != NULL) {
            unsigned int captured = length < SEQUENCE_BYTES_MAX ? length : SEQUENCE_BYTES_MAX;
            memcpy(sequence->tx, spi_tx, captured);
            sequence->has_rx = rx != NULL;
        }
        transfer_capture_t capture = {.control = trace, .sequence = sequence};
        spi_transaction_t transaction = {
            .length = length * 8U,
            .tx_buffer = spi_tx,
            .rx_buffer = rx != NULL ? spi_rx : NULL,
            .user = &capture,
        };
        transaction_error = spi_device_polling_transmit(device, &transaction);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
        robusto_halow_sdspi_check_transfer(&write_check, tx != NULL ? spi_tx : NULL,
            rx != NULL ? spi_rx : NULL, length, transaction_error == ESP_OK);
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        tx_trace_transfer(tx != NULL ? spi_tx : NULL, rx != NULL ? spi_rx : NULL,
                          length, transaction_error);
#endif
        if (sequence != NULL && transaction_error == ESP_OK && rx != NULL) {
            unsigned int captured = length < SEQUENCE_BYTES_MAX ? length : SEQUENCE_BYTES_MAX;
            memcpy(sequence->rx, spi_rx, captured);
        }
        if (transaction_error != ESP_OK) {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(transaction_error));
        } else if (trace_block) {
            if (memcmp(tx, spi_tx, length) != 0) {
                control_buffer_changed = true;
                transaction_error = ESP_ERR_INVALID_STATE;
            }
        }
    }
    if (rx != NULL) {
        if (transaction_error == ESP_OK) {
            memcpy(rx, spi_rx, length);
            if (trace_control_submission && length == 17U &&
                spi_tx[0] == 0xff && spi_tx[1] == 0x75 && (spi_tx[2] & 0x80)) {
                memcpy(control_trace[0].tx, spi_tx, length);
                memcpy(control_trace[0].rx, spi_rx, length);
                control_trace[0].length = length;
            } else if (trace_control_submission && length == 69U && spi_tx[0] == 0xfc) {
                memcpy(control_trace[1].rx, spi_rx, length);
            }
        } else {
            memset(rx, 0xff, length);
        }
        if (trace_initialization && tx != NULL && rx != NULL &&
            length == 18U && spi_tx[0] == 0xff && spi_tx[1] == 0x74) {
            if (init_register_count < 32U) {
                memcpy(init_registers[init_register_count].tx, spi_tx, length);
                memcpy(init_registers[init_register_count].rx, rx, length);
                ++init_register_count;
            } else {
                init_register_overflow = true;
            }
        }
    }
    if (trace_initialization && tx != NULL && rx != NULL &&
        length >= 7U && length <= sizeof(init_commands[0].tx) &&
        spi_tx[0] == 0xff && (spi_tx[1] == 0x40 || spi_tx[1] == 0x45)) {
        unsigned int index = spi_tx[1] == 0x40 ? 0U : 1U;
        memcpy(init_commands[index].tx, spi_tx, length);
        memcpy(init_commands[index].rx, rx, length);
        init_commands[index].length = length;
    }
    finish_sequence(sequence, transaction_error);
}

void spidrv_write_read(void *priv, unsigned char *tx, unsigned char *rx, unsigned int length)
{
    (void)priv;
    transfer(tx, rx, length);
}

void spidrv_write(void *priv, unsigned char *data, unsigned int length, char dma_flag)
{
    (void)priv;
    (void)dma_flag;
    transfer(data, NULL, length);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int length, char dma_flag)
{
    (void)priv;
    (void)dma_flag;
    transfer(NULL, data, length);
}

void spidrv_cs(void *priv, char enable)
{
    (void)priv;
    /* Match the working example: peripheral CS brackets each SPI transaction.
     * Keep vendor operation markers in the trace, without driving GPIO42. */
    sequence_trace_t *sequence = begin_sequence(enable ? '+' : '-', 0);
    esp_err_t result = device != NULL && cs_initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
    finish_sequence(sequence, result);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI CS %s marker without an initialized SPI device: %s",
                 enable ? "begin" : "end",
                 esp_err_to_name(result));
        if (transaction_error == ESP_OK) {
            transaction_error = result;
        }
    }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int length, char flag)
{
    (void)priv;
    (void)data;
    (void)length;
    (void)flag;
    return 0; /* Vendor uses its software CRC implementation. */
}

static esp_err_t check_interface(const char *phase)
{
    transaction_error = ESP_OK;
    int alive = hgic_sdspi_detect_alive(NULL);
    esp_err_t result = transaction_error != ESP_OK ? transaction_error
                                                  : (alive < 0 ? ESP_FAIL : ESP_OK);
    if (HALOW_DIAGNOSTICS_ENABLED || result != ESP_OK) {
        ESP_LOG_LEVEL_LOCAL(result == ESP_OK ? ESP_LOG_INFO : ESP_LOG_ERROR, TAG,
                            "SDSPI health %s: vendor=%d SPI=%s IRQ=%d", phase, alive,
                            esp_err_to_name(transaction_error), gpio_get_level(HALOW_IRQ));
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI health failed; stop/start is required to reinitialize");
    }
    return result;
}

static esp_err_t log_sdspi_registers(void)
{
    static const struct { uint32_t address; const char *name; } registers[] = {
        {0x00, "CCCR_SDIO_VER"}, {0x02, "IO_ENABLE"}, {0x03, "IO_READY"},
        {0x04, "INT_ENABLE"}, {0x07, "BUS_IF_CTRL"}, {0x13, "HIGH_SPEED"},
        {0x110, "FUNC1_BLOCK_SIZE_LO"}, {0x111, "FUNC1_BLOCK_SIZE_HI"},
    };
    for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        uint8_t tx[18], rx[18], status = 0xff, value = 0xff;
        robusto_halow_spi_register_query(tx, registers[i].address);
        transaction_error = ESP_OK;
        spidrv_cs(NULL, 1);
        transfer(tx, rx, sizeof(tx));
        spidrv_cs(NULL, 0);
        bool response = robusto_halow_spi_register_response(rx, &status, &value);
        ESP_LOGI(TAG, "CMD52 read F0 %s[0x%03x]: R5=0x%02x value=0x%02x SPI=%s",
                 registers[i].name, (unsigned int)registers[i].address,
                 status, value, esp_err_to_name(transaction_error));
        if (!response || status != 0 || transaction_error != ESP_OK) {
            ESP_LOGE(TAG, "CMD52 diagnostic read failed (response=%d)", response);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx, sizeof(rx), ESP_LOG_ERROR);
            return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
    }
    return ESP_OK;
}

static int request_window(void)
{
    /* SDSPI rounds writes to 64-byte blocks; even an eight-byte request
     * needs an initialized, block-sized backing allocation. */
    uint8_t request[64] = {0};
    struct hgic_hdr *header = (struct hgic_hdr *)request;
    header->magic = HGIC_HDR_TX_MAGIC;
    header->type = HGIC_HDR_TYPE_SOFTFC;
    header->length = sizeof(*header);
    header->ifidx = 1;
    int sent = hgic_sdspi_write(NULL, request, sizeof(*header));
    if (transaction_error != ESP_OK || sent != (int)sizeof(*header)) {
        ESP_LOGE(TAG, "TX window request failed: %d SPI=%s",
                 sent, esp_err_to_name(transaction_error));
        return -1;
    }
    window_pending = true;
    window_requested = xTaskGetTickCount();
    return 0;
}

int raw_send(unsigned char *data, unsigned int length)
{
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (tx_trace_current != NULL) {
        tx_trace_current->raw_called = true;
        tx_trace_current->raw_bytes = length;
        if (length >= sizeof(struct hgic_frm_hdr2) + sizeof(struct eth_hdr)) {
            const struct hgic_frm_hdr2 *header = (const struct hgic_frm_hdr2 *)data;
            tx_trace_current->header_bytes = header->hdr.length;
            tx_trace_current->cookie = header->hdr.cookie;
            tx_trace_current->ifidx = header->hdr.ifidx;
        }
    }
#endif
    if (transaction_error != ESP_OK || length == 0 || length > 1664U || tx_window < 2) {
        return -1;
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
    bool check_frame = length >= sizeof(struct hgic_frm_hdr2) &&
                       data[2] == HGIC_HDR_TYPE_FRM2;
    int64_t check_started = 0;
    if (check_frame) {
        robusto_halow_sdspi_check_begin(&write_check, data, length);
        check_started = esp_timer_get_time();
    }
#endif
    int sent = hgic_sdspi_write(NULL, data, length);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (tx_trace_current != NULL) tx_trace_current->sdspi_result = sent;
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_WRITE_CHECKS
    if (check_frame) {
        bool valid = robusto_halow_sdspi_check_finish(&write_check, sent);
        ++checked_writes;
        uint32_t size_bit = 1U << write_check.expected_blocks;
        if (!valid || checked_writes <= 4U || !(checked_block_sizes & size_bit) ||
            (checked_writes % 64U) == 0)
            log_write_check(sent, valid, esp_timer_get_time() - check_started);
        checked_block_sizes |= size_bit;
        if (!valid) return -1;
    }
#endif
    if (transaction_error != ESP_OK || sent != (int)length) {
        return -1;
    }
    --tx_window;
    return sent;
}

static void log_vendor_exception(const robusto_halow_spi_packet_t *packet)
{
    const uint8_t *payload = packet->event_payload;
    uint32_t code = get_unaligned_le32(payload);
    switch (code) {
    case HGIC_EXCEPTION_CPU_USED_OVERTOP:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: CPU_USED_OVERTOP");
        break;
    case HGIC_EXCEPTION_HEAP_USED_OVERTOP:
    case HGIC_EXCEPTION_WIFI_BUFFER_USED_OVERTOP:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: %s total=%u used=%u",
                 code == HGIC_EXCEPTION_HEAP_USED_OVERTOP ? "HEAP_USED_OVERTOP" : "WIFI_BUFFER_USED_OVERTOP",
                 (unsigned)get_unaligned_le32(payload + 4),
                 (unsigned)get_unaligned_le32(payload + 8));
        break;
    case HGIC_EXCEPTION_TX_BLOCKED:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: TX_BLOCKED");
        break;
    case HGIC_EXCEPTION_TXDELAY_TOOLONG:
    case HGIC_EXCEPTION_STRONG_BGRSSI:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: %s max=%ld avg=%ld min=%ld",
                 code == HGIC_EXCEPTION_TXDELAY_TOOLONG ? "TXDELAY_TOOLONG" : "STRONG_BGRSSI",
                 (long)(int32_t)get_unaligned_le32(payload + 4),
                 (long)(int32_t)get_unaligned_le32(payload + 8),
                 (long)(int32_t)get_unaligned_le32(payload + 12));
        break;
    case HGIC_EXCEPTION_TEMPERATURE_OVERTOP:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: TEMPERATURE_OVERTOP value=%ld",
                 (long)(int32_t)get_unaligned_le32(payload + 4));
        break;
    case HGIC_EXCEPTION_WRONG_PASSWORD:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: WRONG_PASSWORD");
        break;
    default:
        ESP_LOGE(TAG, "Vendor EXCEPTION_INFO: unknown code=%u payload=%u bytes (payload omitted)",
                 (unsigned)code, (unsigned)packet->event_length);
        break;
    }
}

static void log_vendor_event(const robusto_halow_spi_packet_t *packet)
{
    switch (packet->event_id) {
    case HGIC_EVENT_CONECT_START:
        ESP_LOGI(TAG, "Connection attempt started (event 11)");
        return;
    case HGIC_EVENT_CONNECT_FAIL:
        ESP_LOGW(TAG, "Connection failed (event 22): detail=0x%02x (%u), payload=%u bytes; detail meaning unverified",
                 (unsigned int)packet->event_payload[0],
                 (unsigned int)packet->event_payload[0],
                 (unsigned int)packet->event_length);
        return;
    case HGIC_EVENT_CONECTED:
        ESP_LOGI(TAG, "Vendor CONECTED (event 12): peer=%02x:%02x:%02x:%02x:%02x:%02x",
                 packet->event_payload[0], packet->event_payload[1], packet->event_payload[2],
                 packet->event_payload[3], packet->event_payload[4], packet->event_payload[5]);
        return;
    case HGIC_EVENT_DISCONECTED:
        ESP_LOGI(TAG, "Vendor DISCONECTED (event 13)");
        return;
    case HGIC_EVENT_PAIR_DONE:
        ESP_LOGI(TAG, "Vendor PAIR_DONE (event 10): paired stations=%u",
                 hgic.paired_stas_cnt);
        return;
    case HGIC_EVENT_TX_BITRATE:
        if (bitrate_logs < 4U) {
            ++bitrate_logs;
            ESP_LOGI(TAG, "Vendor TX_BITRATE (event 7): raw=%u (radio status, not SPI clock)",
                     (unsigned)hgic.status.tx_bitrate);
        } else if (bitrate_logs == 4U) {
            ++bitrate_logs;
            ESP_LOGI(TAG, "Further TX_BITRATE event logs suppressed until transport reinitialization");
        }
        return;
    case HGIC_EVENT_SIGNAL:
        if (signal_logs < 4U) {
            ++signal_logs;
            ESP_LOGI(TAG, "Vendor SIGNAL (event 14): rssi=%d evm=%d",
                     hgic.status.rssi, hgic.status.evm);
        } else if (signal_logs == 4U) {
            ++signal_logs;
            ESP_LOGI(TAG, "Further SIGNAL event logs suppressed until transport reinitialization");
        }
        return;
    case HGIC_EVENT_EXCEPTION_INFO:
        log_vendor_exception(packet);
        return;
    case HGIC_EVENT_DISCONNECT_REASON:
    case HGIC_EVENT_ASSOC_STATUS:
    case HGIC_EVENT_DISCONNET_LOG:
    case HGIC_EVENT_REQUEST_PARAM:
    case HGIC_EVENT_TESTMODE_STATE:
    case HGIC_EVENT_SLEEP_FAIL:
    case HGIC_EVENT_STA_MIC_ERROR:
    case ROBUSTO_HALOW_SPI_EVENT_UNDOCUMENTED_31:
        ESP_LOGW(TAG, "Vendor event %s (%u): payload=%u bytes; payload not decoded",
                 robusto_halow_spi_event_name(packet->event_id), packet->event_id,
                 (unsigned)packet->event_length);
        return;
    default:
        ESP_LOGI(TAG, "Vendor event %s (%u): payload=%u bytes; payload not decoded",
                 robusto_halow_spi_event_name(packet->event_id), packet->event_id,
                 (unsigned)packet->event_length);
        return;
    }
}

static int read_packet(robusto_halow_spi_packet_t *packet)
{
    capture_debug_uart();
    transaction_error = ESP_OK;
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last_interface_check) >= pdMS_TO_TICKS(STATUS_INTERVAL_MS)) {
        int alive = hgic_sdspi_detect_alive(NULL);
        if (alive < 0 || transaction_error != ESP_OK) {
            ESP_LOGE(TAG, "SDSPI health check failed: vendor=%d SPI=%s; "
                     "stop/start is required to reinitialize the interface",
                     alive, esp_err_to_name(transaction_error));
            return -1;
        }
        last_interface_check = now;
    }
    int length = hgic_sdspi_read(NULL, rx_buffer, sizeof(rx_buffer), 0);
    if (transaction_error != ESP_OK || length < 0) {
        ESP_LOGE(TAG, "SDSPI receive failed: result=%d SPI=%s",
                 length, esp_err_to_name(transaction_error));
        return -1;
    }
    if (length == 0) {
        return 0;
    }
#ifdef CONFIG_ROBUSTO_HALOW_RX_TRACE
    ++rx_trace_reads;
    if (rx_trace_reads <= 128U || rx_trace_reads % 64U == 0) {
        bool header = length >= sizeof(struct hgic_hdr);
        ESP_LOGI(TAG,
                 "RXTRACE SPI read=%lu unvalidated=1 bytes=%d header=%u magic=0x%04x type=%u declared=%u cookie=%u",
                 (unsigned long)rx_trace_reads, length, header,
                 header ? get_unaligned_le16(rx_buffer) : 0,
                 header ? rx_buffer[2] : 0,
                 header ? get_unaligned_le16(rx_buffer + 4) : 0,
                 header ? get_unaligned_le16(rx_buffer + 6) : 0);
    }
#endif
    if (!robusto_halow_spi_parse(rx_buffer, length, packet)) {
        if (packet->unsupported && packet->event) {
            ESP_LOGW(TAG, "Unknown vendor event %u (declared=%u read=%d bytes)",
                     packet->event_id, get_unaligned_le16(rx_buffer + 4), length);
        } else {
            ESP_LOGW(TAG, "%s vendor packet (read=%d bytes)",
                     packet->unsupported ? "Unsupported" : "Malformed", length);
        }
        if (rejected_header_logs < 4U) {
            ++rejected_header_logs;
            unsigned int header_length = (unsigned int)length < CMD_HDR_LEN
                                             ? (unsigned int)length : CMD_HDR_LEN;
            ESP_LOGW(TAG, "Rejected vendor header (payload omitted):");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, header_length, ESP_LOG_WARN);
        }
        return 0;
    }
    if (packet->length > 0) {
        ++rx_data_frames;
        if (rx_data_frames == 1 || rx_data_frames % 64U == 0) {
            ESP_LOGI(TAG, "RX SPI data frames=%lu bytes=%u cookie=%u",
                     (unsigned long)rx_data_frames, (unsigned int)packet->length,
                     packet->cookie_received);
        }
    }
    if (packet->cookie_discontinuity) {
        ESP_LOGW(TAG, "RX cookie discontinuity: previous=%u expected=%u received=%u; frame retained",
                 packet->cookie_previous, packet->cookie_expected, packet->cookie_received);
    }
    if (packet->event) log_vendor_event(packet);
    if (packet->flow_control) {
        tx_window = packet->tx_window;
        window_pending = false;
    }
    return 1;
}

static esp_err_t submit_command(int (*send)(void), uint16_t id)
{
    TickType_t window_begin = xTaskGetTickCount();
    while (tx_window < 2) {
        transaction_error = ESP_OK;
        if (!window_pending && request_window() < 0) {
            ESP_LOGE(TAG, "Command %u TX window request failed", id);
            return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
        robusto_halow_spi_packet_t packet;
        if (read_packet(&packet) < 0) {
            ESP_LOGE(TAG, "Command %u TX window receive failed", id);
            return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
        if ((TickType_t)(xTaskGetTickCount() - window_begin) >=
            pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "Command %u TX window timed out", id);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    transaction_error = ESP_OK;
    const bool capture_submission = HALOW_DIAGNOSTICS_ENABLED &&
                                   (id == HGIC_CMD_GET_FW_INFO ||
                                    id == HGIC_CMD_SET_WORK_MODE ||
                                    id == HGIC_CMD_GET_CONN_STATE ||
                                    id == HGIC_CMD_GET_MODE ||
                                    id == HGIC_CMD_RADIO_ONOFF ||
                                    id == HGIC_CMD_RESET);
    if (capture_submission) {
        memset(control_trace, 0, sizeof(control_trace));
        memset(sequence_trace, 0, sizeof(sequence_trace));
        sequence_count = 0;
        sequence_dropped = 0;
        control_buffer_changed = false;
        sequence_begin_us = esp_timer_get_time();
    }
    trace_control_submission = capture_submission;
    int sent = send();
    trace_control_submission = false;
    if (capture_submission) {
        log_submission_sequence(id);
        for (unsigned int i = 0; i < 2U; ++i) {
            if (control_trace[i].length != 0) {
                ESP_LOGI(TAG, "Control %s captured TX then RX (%u bytes):",
                         i == 0 ? "CMD53" : "block", control_trace[i].length);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, control_trace[i].tx, control_trace[i].length, ESP_LOG_INFO);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, control_trace[i].rx, control_trace[i].length, ESP_LOG_INFO);
                const control_trace_t *trace = &control_trace[i];
                ESP_LOGI(TAG, "DMA TX: buffer=%p length=%u size=%u single=%d SPI_bits=%u LSB_first=%d captured=%d",
                         (void *)trace->dma_address, trace->dma_length, trace->dma_size,
                         trace->dma_single, trace->spi_bits, trace->tx_lsb_first,
                         trace->dma_captured);
                if (trace->dma_captured) {
                    ESP_LOGI(TAG, "DMA TX backing-memory bytes before SPI:");
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, trace->dma_tx, trace->length, ESP_LOG_INFO);
                    bool matches = memcmp(trace->tx, trace->dma_tx, trace->length) == 0;
                    if (!matches || trace->spi_bits != trace->length * 8U ||
                        trace->tx_lsb_first) {
                        ESP_LOGE(TAG, "DMA submission mismatch: bytes_match=%d", matches);
                        transaction_error = ESP_ERR_INVALID_STATE;
                    } else {
                        ESP_LOGI(TAG, "DMA submission matches captured host TX bytes and bit length");
                    }
                } else {
                    ESP_LOGE(TAG, "DMA submission capture unavailable or unexpected descriptor layout");
                    transaction_error = ESP_ERR_INVALID_STATE;
                }
            }
        }
        if (control_buffer_changed) {
            ESP_LOGE(TAG, "Control block TX buffer changed during SPI transfer");
        } else if (control_trace[1].length != 0 && transaction_error == ESP_OK) {
            ESP_LOGI(TAG, "Control block TX buffer unchanged after SPI completion");
        }
    }
    if (sent < 0 || transaction_error != ESP_OK) {
        ESP_LOGE(TAG, "Command %u submission failed", id);
        return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
    }
    if (HALOW_DIAGNOSTICS_ENABLED) {
        ESP_LOGI(TAG, "Command %u submitted: %d bytes", id, sent);
    }
    return ESP_OK;
}

#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
static bool string_setting_matches(const robusto_halow_spi_packet_t *packet,
                                   const char *expected)
{
    size_t length = strlen(expected);
    return (packet->command_length == length ||
            (packet->command_length == length + 1 &&
             packet->command_payload[length] == '\0')) &&
           memcmp(packet->command_payload, expected, length) == 0;
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
#define NETWORK_ROLE "STA"
#define NETWORK_BANDWIDTH CONFIG_ROBUSTO_HALOW_SPI_STA_BSS_BW
#define NETWORK_FREQUENCY_LIST CONFIG_ROBUSTO_HALOW_SPI_STA_FREQ_LIST
static char network_ssid[] = CONFIG_ROBUSTO_HALOW_SPI_STA_SSID;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_WPA_PSK
#define NETWORK_WPA_PSK
static char network_psk[] = CONFIG_ROBUSTO_HALOW_SPI_STA_PSK;
#endif
#else
#define NETWORK_ROLE "AP"
#define NETWORK_BANDWIDTH CONFIG_ROBUSTO_HALOW_SPI_AP_BSS_BW
#define NETWORK_FREQUENCY_LIST CONFIG_ROBUSTO_HALOW_SPI_AP_FREQ_LIST
static char network_ssid[] = CONFIG_ROBUSTO_HALOW_SPI_AP_SSID;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_AP_WPA_PSK
#define NETWORK_WPA_PSK
static char network_psk[] = CONFIG_ROBUSTO_HALOW_SPI_AP_PSK;
#endif
#endif
static unsigned short network_frequencies[16];
static int network_frequency_count;

static bool parse_network_frequencies(const char *text, unsigned short frequencies[16],
                                 int *count)
{
    *count = 0;
    while (*text != '\0') {
        if (*count == 16 || *text < '0' || *text > '9') return false;
        unsigned int value = 0;
        do {
            value = value * 10U + (unsigned int)(*text++ - '0');
            if (value > UINT16_MAX) return false;
        } while (*text >= '0' && *text <= '9');
        if (value == 0) return false;
        for (int i = 0; i < *count; ++i) {
            if (frequencies[i] == value) return false;
        }
        frequencies[(*count)++] = (unsigned short)value;
        if (*text == '\0') return true;
        if (*text++ != ',' || *text == '\0') return false;
    }
    return false;
}

static esp_err_t validate_network_configuration(void)
{
    size_t ssid_length = strlen(network_ssid);
    if (ssid_length == 0 || ssid_length > 31) {
        ESP_LOGE(TAG, "%s SSID must contain 1-31 bytes", NETWORK_ROLE);
        return ESP_ERR_INVALID_ARG;
    }
#ifdef NETWORK_WPA_PSK
    if (strlen(network_psk) != 64) {
        ESP_LOGE(TAG, "%s WPA-PSK requires exactly 64 hexadecimal digits", NETWORK_ROLE);
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < 64; ++i) {
        char c = network_psk[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            ESP_LOGE(TAG, "%s WPA-PSK contains a non-hexadecimal digit", NETWORK_ROLE);
            return ESP_ERR_INVALID_ARG;
        }
    }
#endif
    if (NETWORK_BANDWIDTH != 1 && NETWORK_BANDWIDTH != 2 &&
        NETWORK_BANDWIDTH != 4 && NETWORK_BANDWIDTH != 8) {
        ESP_LOGE(TAG, "%s bandwidth must be 1, 2, 4 or 8 MHz", NETWORK_ROLE);
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_network_frequencies(NETWORK_FREQUENCY_LIST,
                                   network_frequencies, &network_frequency_count)) {
        ESP_LOGE(TAG, "%s frequency list must contain 1-16 distinct positive 16-bit integers", NETWORK_ROLE);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static bool network_setting_matches(const robusto_halow_spi_packet_t *packet)
{
    const char *expected;
    switch (packet->command_id) {
    case HGIC_CMD_GET_SSID:
        expected = network_ssid;
        break;
    case HGIC_CMD_GET_KEY_MGMT:
#ifdef NETWORK_WPA_PSK
        expected = "WPA-PSK";
#else
        expected = "NONE";
#endif
        break;
    case HGIC_CMD_GET_BSS_BW:
        return packet->command_status == NETWORK_BANDWIDTH;
    case HGIC_CMD_GET_CHAN_LIST:
        if (packet->command_status != network_frequency_count * 2 ||
            packet->command_length < (size_t)network_frequency_count * 2) return false;
        for (int i = 0; i < network_frequency_count; ++i) {
            if (get_unaligned_le16(packet->command_payload + i * 2) != network_frequencies[i])
                return false;
        }
        return true;
    default:
        return true;
    }
    return string_setting_matches(packet, expected);
}
#endif

static esp_err_t command(int (*send)(void), uint16_t id)
{
    esp_err_t result = submit_command(send, id);
    if (result != ESP_OK) {
        return result;
    }
    TickType_t begin = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - begin) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
        robusto_halow_spi_packet_t packet;
        int received = read_packet(&packet);
        if (received < 0) {
            ESP_LOGE(TAG, "Command %u response receive failed", id);
            return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
        if (received > 0 && packet.command && packet.command_id == id) {
            if (packet.command_status < 0) {
                ESP_LOGE(TAG, "Command %u rejected: %d", id, packet.command_status);
                return ESP_FAIL;
            }
            if (id == HGIC_CMD_GET_MODE && !packet.ap_mode) {
                ESP_LOGE(TAG, "Module did not confirm the configured AP role");
                return ESP_ERR_INVALID_STATE;
            }
#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
            if (!network_setting_matches(&packet)) {
                ESP_LOGE(TAG, "%s configuration readback mismatch for command %u", NETWORK_ROLE, id);
                return ESP_ERR_INVALID_STATE;
            }
#endif
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "Command %u response timed out", id);
    return ESP_ERR_TIMEOUT;
}

#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
static int set_network_ssid(void) { return hgic_raw_set_ssid(network_ssid); }
static int set_network_security(void)
{
#ifdef NETWORK_WPA_PSK
    return hgic_raw_set_key_mgmt("WPA-PSK");
#else
    return hgic_raw_set_key_mgmt("NONE");
#endif
}
#ifdef NETWORK_WPA_PSK
static int set_network_psk(void) { return hgic_raw_set_wpa_psk(network_psk); }
#endif
static int set_network_bandwidth(void)
{ return hgic_raw_set_bss_bw(NETWORK_BANDWIDTH); }
static int set_network_frequencies(void)
{ return hgic_raw_set_chan_list(network_frequencies, network_frequency_count); }

static esp_err_t apply_network_configuration(void)
{
    ESP_LOGW(TAG, "Applying configured %s settings; module auto-save policy may persist changes", NETWORK_ROLE);
    esp_err_t result = command(set_network_ssid, HGIC_CMD_SET_SSID);
#ifdef NETWORK_WPA_PSK
    if (result == ESP_OK) result = command(set_network_psk, HGIC_CMD_SET_WPA_PSK);
#endif
    if (result == ESP_OK) result = command(set_network_security, HGIC_CMD_SET_KEY_MGMT);
    if (result == ESP_OK) result = command(set_network_bandwidth, HGIC_CMD_SET_BSS_BW);
    if (result == ESP_OK) result = command(set_network_frequencies, HGIC_CMD_SET_CHAN_LIST);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s configuration failed; module settings may be partially changed; no rollback or retry", NETWORK_ROLE);
    }
    return result;
}

static esp_err_t verify_network_configuration(void)
{
    esp_err_t result = command(hgic_raw_get_ssid, HGIC_CMD_GET_SSID);
    if (result == ESP_OK) result = command(hgic_raw_get_key_mgmt, HGIC_CMD_GET_KEY_MGMT);
    if (result == ESP_OK) result = command(hgic_raw_get_bss_bw, HGIC_CMD_GET_BSS_BW);
    if (result == ESP_OK) result = command(hgic_raw_get_chan_list, HGIC_CMD_GET_CHAN_LIST);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "%s SSID/security/bandwidth/frequency-list readback verified", NETWORK_ROLE);
#ifdef NETWORK_WPA_PSK
        ESP_LOGI(TAG, "PSK write acknowledged, key not queried");
#endif
    }
    return result;
}
#endif

#ifdef HALOW_CONFIGURE_MINIMUM_MCS
#define HALOW_STRINGIFY_INNER(value) #value
#define HALOW_STRINGIFY(value) HALOW_STRINGIFY_INNER(value)
static char minimum_mcs_set[] =
    "AT+TX_MCS_MIN=" HALOW_STRINGIFY(CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN);
static char minimum_mcs_query[] = "AT+TX_MCS_MIN=?";
static const char minimum_mcs_report[] =
    "limit tx mcs min= " HALOW_STRINGIFY(CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN) "\r\n";
#undef HALOW_STRINGIFY
#undef HALOW_STRINGIFY_INNER

static int send_minimum_mcs(void) { return hgic_raw_set_atcmd(minimum_mcs_set); }
static int query_minimum_mcs(void) { return hgic_raw_set_atcmd(minimum_mcs_query); }

static esp_err_t minimum_mcs_exchange(int (*send)(void))
{
    esp_err_t result = submit_command(send, HGIC_CMD_SET_ATCMD);
    if (result != ESP_OK) return result;
    bool acknowledged = false;
    bool reported = false;
    TickType_t begin = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - begin) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
        robusto_halow_spi_packet_t packet;
        int received = read_packet(&packet);
        if (received < 0) {
            ESP_LOGE(TAG, "TX_MCS_MIN response receive failed");
            return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
        if (received > 0) {
            if (packet.command && packet.command_id == HGIC_CMD_SET_ATCMD) {
                if (packet.command_status < 0) {
                    ESP_LOGE(TAG, "TX_MCS_MIN command rejected: %d", packet.command_status);
                    return ESP_FAIL;
                }
                acknowledged = true;
            }
            static const char prefix[] = "limit tx mcs min=";
            if (packet.event && packet.event_id == HGIC_EVENT_FWDBG_INFO &&
                packet.event_length >= sizeof(prefix) - 1 &&
                memcmp(packet.event_payload, prefix, sizeof(prefix) - 1) == 0) {
                size_t expected = sizeof(minimum_mcs_report) - 1;
                if (!((packet.event_length == expected ||
                       (packet.event_length == expected + 1 &&
                        packet.event_payload[expected] == '\0')) &&
                      memcmp(packet.event_payload, minimum_mcs_report, expected) == 0)) {
                    ESP_LOGE(TAG, "TX_MCS_MIN value report mismatch: expected=%d bytes=%u",
                             CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN,
                             (unsigned int)packet.event_length);
                    return ESP_ERR_INVALID_STATE;
                }
                reported = true;
            }
            if (acknowledged && reported) return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "TX_MCS_MIN response timed out: acknowledged=%u value_report=%u",
             acknowledged, reported);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t configure_minimum_mcs(void)
{
    esp_err_t result = minimum_mcs_exchange(send_minimum_mcs);
    if (result == ESP_OK) result = minimum_mcs_exchange(query_minimum_mcs);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "TX_MCS_MIN=%d set and independently queried/readback verified",
                 CONFIG_ROBUSTO_HALOW_SPI_TX_MCS_MIN);
    } else {
        ESP_LOGE(TAG, "TX_MCS_MIN configuration failed; startup readiness blocked");
    }
    return result;
}
#endif

static int set_configured_mode(void)
{
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    return hgic_raw_set_mode("ap");
#else
    return hgic_raw_set_mode("sta");
#endif
}

static int enable_radio(void)
{
    return hgic_raw_set_radio_onoff(1);
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START
static int disable_radio(void)
{
    return hgic_raw_set_radio_onoff(0);
}
#endif

static int query_role_status(void)
{
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    return hgic_raw_get_mode();
#else
    return hgic_raw_get_connect_state();
#endif
}

static bool radio_ready(void)
{
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
    return ap_interface_ready;
#else
    return hgic.status.conn_state != 0;
#endif
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
static int send_ap_probe(void)
{
    uint8_t request[64] = {0};
    struct hgic_hdr *header = (struct hgic_hdr *)request;
    header->magic = HGIC_HDR_TX_MAGIC;
    header->type = HGIC_HDR_TYPE_TEST2;
    header->length = sizeof(*header) + sizeof(ROBUSTO_HALOW_SPI_AP_PROBE) - 1;
    header->ifidx = 1;
    memcpy(request + sizeof(*header), ROBUSTO_HALOW_SPI_AP_PROBE,
           sizeof(ROBUSTO_HALOW_SPI_AP_PROBE) - 1);
    return raw_send(request, header->length);
}

static esp_err_t probe_ap_interface(void)
{
    /* TEST2 is the pinned driver's local interface echo, not an RF receipt. */
    esp_err_t result = submit_command(send_ap_probe, 0);
    if (result != ESP_OK) return result;
    TickType_t begin = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - begin) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
        robusto_halow_spi_packet_t packet;
        int received = read_packet(&packet);
        if (received < 0) return ESP_FAIL;
        if (received > 0 && packet.loopback) {
            if (packet.loopback_valid) return ESP_OK;
            ESP_LOGE(TAG, "AP raw-interface loopback payload mismatch");
            return ESP_ERR_INVALID_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "AP raw-interface loopback timed out");
    return ESP_ERR_TIMEOUT;
}
#endif

#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
static uint8_t echo_request[ROBUSTO_HALOW_SPI_FRAME_MAX + 22U];
static uint32_t echo_serial;

static int send_echo_request(void)
{
    return raw_send(echo_request, get_unaligned_le16(echo_request + 4));
}

static esp_err_t run_echo_sweep(void)
{
    const unsigned int raw_sizes[] = {256, 384, 448, 490, 491, 512, 1024};
    ESP_LOGW(TAG, "SPI_ECHO starting module-local byte-exact sweep at unchanged 40 MHz; "
             "not an RF test; readiness withheld until all 70 trials pass");
    for (unsigned int size = 0; size < sizeof(raw_sizes) / sizeof(raw_sizes[0]); ++size) {
        unsigned int total = raw_sizes[size] + 22U;
        unsigned int payload_length = total - sizeof(struct hgic_hdr);
        int64_t min_us = INT64_MAX, max_us = 0;
        for (unsigned int trial = 1; trial <= 10; ++trial) {
            if (echo_serial == UINT32_MAX) {
                ESP_LOGE(TAG, "SPI_ECHO identity exhausted; no wraparound or retry");
                return ESP_ERR_INVALID_STATE;
            }
            ++echo_serial;
            memset(echo_request, 0, sizeof(echo_request));
            struct hgic_hdr *header = (struct hgic_hdr *)echo_request;
            header->magic = HGIC_HDR_TX_MAGIC;
            header->type = HGIC_HDR_TYPE_TEST2;
            header->length = total;
            header->ifidx = 1;
            uint8_t *payload = echo_request + sizeof(*header);
            for (unsigned int i = 0; i < payload_length; ++i)
                payload[i] = (uint8_t)((i * 37U) ^ (i >> 8) ^
                                       (echo_serial * 53U) ^ (echo_serial >> 8));
            memcpy(payload, "RHEC", 4);
            put_unaligned_le32(echo_serial, payload + 4);
            int64_t started = esp_timer_get_time();
            esp_err_t result = submit_command(send_echo_request, 0);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "SPI_ECHO FAIL raw_equiv=%u vendor=%u trial=%u/10 id=%lu submission=%s",
                         raw_sizes[size], total, trial, (unsigned long)echo_serial,
                         esp_err_to_name(result));
                return result;
            }
            bool matched = false;
            TickType_t begin = xTaskGetTickCount();
            while ((TickType_t)(xTaskGetTickCount() - begin) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
                robusto_halow_spi_packet_t packet;
                int received = read_packet(&packet);
                if (received < 0) {
                    ESP_LOGE(TAG, "SPI_ECHO FAIL raw_equiv=%u vendor=%u trial=%u/10 id=%lu receive error",
                             raw_sizes[size], total, trial, (unsigned long)echo_serial);
                    return transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
                }
                if (received > 0 && packet.loopback) {
                    if (packet.loopback_length != payload_length) {
                        ESP_LOGE(TAG, "SPI_ECHO FAIL raw_equiv=%u vendor=%u trial=%u/10 id=%lu"
                                 " payload length expected=%u received=%u",
                                 raw_sizes[size], total, trial, (unsigned long)echo_serial,
                                 payload_length, (unsigned int)packet.loopback_length);
                        return ESP_ERR_INVALID_SIZE;
                    }
                    for (unsigned int i = 0; i < payload_length; ++i) {
                        if (packet.loopback_payload[i] != payload[i]) {
                            ESP_LOGE(TAG, "SPI_ECHO FAIL raw_equiv=%u vendor=%u trial=%u/10 id=%lu"
                                     " payload mismatch offset=%u",
                                     raw_sizes[size], total, trial, (unsigned long)echo_serial, i);
                            return ESP_ERR_INVALID_STATE;
                        }
                    }
                    matched = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!matched) {
                ESP_LOGE(TAG, "SPI_ECHO FAIL raw_equiv=%u vendor=%u trial=%u/10 id=%lu timeout",
                         raw_sizes[size], total, trial, (unsigned long)echo_serial);
                return ESP_ERR_TIMEOUT;
            }
            int64_t elapsed = esp_timer_get_time() - started;
            if (elapsed < min_us) min_us = elapsed;
            if (elapsed > max_us) max_us = elapsed;
        }
        ESP_LOGI(TAG, "SPI_ECHO PASS raw_equiv=%u vendor=%u blocks=%u verified=10/10"
                 " payload_bytes=%u elapsed_us=%lld..%lld",
                 raw_sizes[size], total, (total + 63U) / 64U, payload_length,
                 (long long)min_us, (long long)max_us);
    }
    ESP_LOGI(TAG, "SPI_ECHO complete: 70/70 byte-exact local echoes; not proof of RF or normal data-path delivery");
    return ESP_OK;
}
#endif

static esp_err_t release_hardware(void)
{
    esp_err_t result = stop_debug_capture();
    if (result != ESP_OK) {
        return result;
    }
    if (device != NULL) {
        result = spi_bus_remove_device(device);
        if (result != ESP_OK) {
            return result;
        }
        device = NULL;
    }
    if (cs_initialized) {
        result = gpio_reset_pin(HALOW_CS);
        if (result != ESP_OK) {
            return result;
        }
        cs_initialized = false;
    }
    if (bus_initialized) {
        result = spi_bus_free(SPI2_HOST);
        if (result != ESP_OK) {
            return result;
        }
        bus_initialized = false;
        tx_descriptor = NULL;
    }
    if (irq_initialized) {
        result = gpio_reset_pin(HALOW_IRQ);
        if (result != ESP_OK) {
            return result;
        }
        irq_initialized = false;
    }
    return ESP_OK;
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
static void frm2_sweep_report(frm2_sweep_rx_t *state, const char *reason)
{
    unsigned int total = 0;
    bool byte_scan = state->mode == FRM2_SWEEP_BYTE_SCAN;
    bool tight_scan = state->mode == FRM2_SWEEP_TIGHT_SCAN;
    unsigned int groups = byte_scan ? FRM2_BYTE_SCAN_SIZES : 6U;
    unsigned int trials = tight_scan ? 5U : byte_scan ? 3U : 10U;
    uint16_t order[FRM2_TIGHT_SCAN_COUNT];
    if (tight_scan) frm2_tight_order(state->run, order);
    bool monotonic = true, missing = false;
    unsigned int mixed = 0;
    int last_full = -1, first_missing = -1;
    for (unsigned int group = 0; group < groups; ++group) {
        unsigned int verified = 0;
        unsigned int size = tight_scan ? FRM2_TIGHT_SCAN_MIN + group :
                            byte_scan ? FRM2_BYTE_SCAN_MIN + group : frm2_sweep_sizes[group];
        for (unsigned int trial = 0; trial < trials; ++trial) {
            unsigned int sequence = byte_scan ?
                trial * FRM2_BYTE_SCAN_SIZES +
                (trial == 1U ? FRM2_BYTE_SCAN_SIZES - 1U - group : group) + 1U :
                group * 10U + trial + 1U;
            if (tight_scan) {
                sequence = 0;
                unsigned int occurrence = 0;
                for (unsigned int i = 0; i < FRM2_TIGHT_SCAN_COUNT; ++i) {
                    if (order[i] == size && occurrence++ == trial) {
                        sequence = i + 1U;
                        break;
                    }
                }
                if (sequence == 0) {
                    ESP_LOGE(TAG, "FRM2_TIGHT_SCAN invalid schedule size=%u trial=%u; no summary",
                             size, trial + 1U);
                    state->reported = true;
                    return;
                }
            }
            if (frm2_sweep_has(state, sequence)) {
                ++verified;
            } else {
                ESP_LOGE(TAG, "FRM2_SWEEP MISSING run=%llu seq=%u raw=%u trial=%u",
                         (unsigned long long)state->run, sequence, size, trial + 1U);
            }
        }
        total += verified;
        ESP_LOGI(TAG, "FRM2_SWEEP RX size=%u verified=%u/%u run=%llu",
                 size, verified, trials, (unsigned long long)state->run);
        if (verified == trials) {
            last_full = (int)size;
            if (missing) monotonic = false;
        } else if (verified == 0) {
            if (first_missing < 0) first_missing = (int)size;
            missing = true;
        } else {
            ++mixed;
            monotonic = false;
        }
    }
    if (byte_scan || tight_scan) {
        bool established = monotonic && last_full >= 0 && first_missing == last_full + 1 &&
                           state->start && state->end && !state->corrupt && !state->duplicates &&
                           strcmp(reason, "end+grace") == 0;
        ESP_LOG_LEVEL_LOCAL(established ? ESP_LOG_INFO : ESP_LOG_WARN, TAG,
            "FRM2_%s_SCAN boundary established=%u last_full=%d first_missing=%d mixed_sizes=%u monotonic=%u run=%llu; observed trials only",
            tight_scan ? "TIGHT" : "BYTE", established, last_full, first_missing, mixed, monotonic,
            (unsigned long long)state->run);
        if (established) {
            ESP_LOGI(TAG, "FRM2_%s_SCAN adjacent raw=%u/%u vendor=%u/%u blocks=%u/%u",
                     tight_scan ? "TIGHT" : "BYTE", (unsigned int)last_full, (unsigned int)first_missing,
                     (unsigned int)last_full + 22U, (unsigned int)first_missing + 22U,
                     ((unsigned int)last_full + 85U) / 64U,
                     ((unsigned int)first_missing + 85U) / 64U);
        }
    }
    bool pass = total == frm2_sweep_count(state->mode) && state->start && state->end &&
                state->corrupt == 0 && state->duplicates == 0;
    ESP_LOG_LEVEL_LOCAL(pass ? ESP_LOG_INFO : ESP_LOG_ERROR, TAG,
        "FRM2_SWEEP RX %s run=%llu verified=%u/%u start=%u end=%u corrupt=%u duplicates=%u reason=%s",
        pass ? "PASS" : "FAIL", (unsigned long long)state->run, total,
        frm2_sweep_count(state->mode),
        state->start, state->end, state->corrupt, state->duplicates, reason);
    state->reported = true;
}

static bool frm2_sweep_receive(frm2_sweep_rx_t *state, const uint8_t source[6],
                               const uint8_t *frame, unsigned int length, int64_t now)
{
    if (!frm2_sweep_candidate(frame, length)) return false;
    uint64_t run;
    uint16_t sequence;
    int bad = frm2_sweep_validate(frame, length, &run, &sequence);
    if (bad >= 0) {
        if (state->active && !state->reported) ++state->corrupt;
        ESP_LOGE(TAG, "FRM2_SWEEP CORRUPT unvalidated=1 raw=%u offset=%d", length, bad);
        return true;
    }
    frm2_sweep_mode_t mode = frame[7] == '3' ? FRM2_SWEEP_TIGHT_SCAN :
                             frame[7] == '2' ? FRM2_SWEEP_BYTE_SCAN : FRM2_SWEEP_COARSE;
    if (state->active && (run != state->run || mode != state->mode ||
                          memcmp(source, state->source, 6) != 0)) {
        ESP_LOGE(TAG, "FRM2_SWEEP unexpected source/run/mode=%llu seq=%u; active run=%llu retained",
                 (unsigned long long)run, sequence, (unsigned long long)state->run);
        return true;
    }
    if (state->reported) {
        ESP_LOGE(TAG, "FRM2_SWEEP late packet run=%llu seq=%u; summary unchanged",
                 (unsigned long long)run, sequence);
        return true;
    }
    if (!state->active) {
        state->active = true;
        state->run = run;
        state->mode = mode;
        state->began = now;
        memcpy(state->source, source, 6);
        ESP_LOGI(TAG, "FRM2_SWEEP RX begin run=%llu byte_scan=%u tight_scan=%u src=%02x:%02x:%02x:%02x:%02x:%02x",
                 (unsigned long long)run, mode == FRM2_SWEEP_BYTE_SCAN,
                 mode == FRM2_SWEEP_TIGHT_SCAN, source[0], source[1], source[2],
                 source[3], source[4], source[5]);
    }
    if (sequence == 0) {
        if (state->start) ++state->duplicates;
        state->start = true;
    } else if (sequence == frm2_sweep_end(state->mode)) {
        if (state->end) ++state->duplicates;
        if (!state->end) state->ended = now;
        state->end = true;
    } else {
        unsigned int index = sequence - 1U;
        uint64_t bit = UINT64_C(1) << (index % 64U);
        if (frm2_sweep_has(state, sequence)) {
            ++state->duplicates;
            ESP_LOGE(TAG, "FRM2_SWEEP DUPLICATE run=%llu seq=%u",
                     (unsigned long long)run, sequence);
        } else {
            state->seen[index / 64U] |= bit;
            ESP_LOGI(TAG, "FRM2_SWEEP RX verified run=%llu seq=%u raw=%u",
                     (unsigned long long)run, sequence, length);
        }
    }
    return true;
}

static void frm2_sweep_poll(frm2_sweep_rx_t *state, int64_t now)
{
    if (!state->active || state->reported) return;
    if (state->end && now - state->ended >= FRM2_SWEEP_GRACE_US)
        frm2_sweep_report(state, "end+grace");
    else if (now - state->began >= (state->mode == FRM2_SWEEP_BYTE_SCAN ? FRM2_BYTE_SCAN_TIMEOUT_US :
                                                     FRM2_SWEEP_TIMEOUT_US))
        frm2_sweep_report(state, "timeout");
}

#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
static int backend_write(void *context, const uint8_t destination[6],
                         const uint8_t *frame, uint32_t length);

static void frm2_sweep_select_peer(frm2_sweep_tx_t *state, const uint8_t source[6],
                                   int64_t now)
{
    static const uint8_t zero[6] = {0};
    if (state->selected || state->done || (source[0] & 1U) || memcmp(source, zero, 6) == 0) return;
    state->selected = true;
    state->run = (uint64_t)now;
    if (state->mode == FRM2_SWEEP_TIGHT_SCAN) {
        uint32_t high = esp_random();
        uint32_t low = esp_random();
        state->run = ((uint64_t)high << 32U) | low;
        if (state->run == 0) {
            ESP_LOGE(TAG, "FRM2_TIGHT_SCAN TX FAIL: random run identity zero; aborted, no retry");
            state->done = true;
            return;
        }
    }
    memcpy(state->destination, source, 6);
    state->due = now + 5000000LL;
    ESP_LOGI(TAG, "FRM2_SWEEP TX selected dst=%02x:%02x:%02x:%02x:%02x:%02x run=%llu byte_scan=%u tight_scan=%u; start in 5s",
             source[0], source[1], source[2], source[3], source[4], source[5],
             (unsigned long long)state->run, state->mode == FRM2_SWEEP_BYTE_SCAN,
             state->mode == FRM2_SWEEP_TIGHT_SCAN);
}

static void frm2_sweep_transmit(frm2_sweep_tx_t *state, int64_t now)
{
    if (!state->selected || state->done || now < state->due) return;
    uint8_t frame[512];
    uint16_t length = frm2_sweep_build(frame, state->run, state->sequence, state->mode);
    int sent = backend_write(NULL, state->destination, frame, length);
    if (sent != length) {
        ESP_LOGE(TAG, "FRM2_SWEEP TX FAIL run=%llu seq=%u raw=%u rc=%d; aborted, no retry",
                 (unsigned long long)state->run, state->sequence, length, sent);
        state->done = true;
        return;
    }
    ESP_LOGI(TAG, "FRM2_SWEEP TX submitted run=%llu seq=%u raw=%u vendor=%u; delivery unconfirmed",
             (unsigned long long)state->run, state->sequence, length, length + 22U);
    if (state->sequence == frm2_sweep_end(state->mode)) {
        state->done = true;
        ESP_LOGI(TAG, "FRM2_SWEEP TX complete: %u data packets submitted; use peer RX summary for delivery",
                 frm2_sweep_count(state->mode));
    } else {
        ++state->sequence;
        state->due = now + FRM2_SWEEP_INTERVAL_US;
    }
}
#endif
#endif

static void receive_task(void *argument)
{
    (void)argument;
    bool published = false;
    TickType_t last_query = xTaskGetTickCount();
    TickType_t last_status = last_query;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    frm2_sweep_rx_t sweep_rx = {0};
    ESP_LOGI(TAG, "FRM2_SWEEP RX armed; supports=HLOWFS01,HLOWFS02,HLOWFS03; requires peer sender; idle is not PASS");
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
    frm2_sweep_tx_t sweep_tx = {0};
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_TIGHT_SCAN_TX
    sweep_tx.mode = FRM2_SWEEP_TIGHT_SCAN;
#elif defined(CONFIG_ROBUSTO_HALOW_SPI_FRM2_BYTE_SCAN_TX)
    sweep_tx.mode = FRM2_SWEEP_BYTE_SCAN;
#endif
    ESP_LOGI(TAG, "FRM2_SWEEP TX armed; byte_scan=%u tight_scan=%u data_packets=%u; waiting for first normal unicast-source frame",
             sweep_tx.mode == FRM2_SWEEP_BYTE_SCAN, sweep_tx.mode == FRM2_SWEEP_TIGHT_SCAN,
             frm2_sweep_count(sweep_tx.mode));
#endif
#endif
    for (;;) {
        if (!take_lock()) {
            ESP_LOGE(TAG, "Receive state lock timed out");
            break;
        }
        if (stopping || faulted) {
            xSemaphoreGive(lock);
            break;
        }
        robusto_halow_spi_packet_t packet;
        int result = read_packet(&packet);
        TickType_t now = xTaskGetTickCount();
        if (result > 0 && packet.command &&
            packet.command_id == HALOW_STATUS_COMMAND) {
            if (packet.command_status < 0 ||
                (HALOW_STATUS_COMMAND == HGIC_CMD_GET_MODE && !packet.ap_mode)) {
                ESP_LOGE(TAG, "%s status rejected or configured role changed", HALOW_ROLE_NAME);
                result = -1;
            } else {
                last_status = now;
            }
        }
        if ((TickType_t)(now - last_status) >= pdMS_TO_TICKS(STATUS_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "%s status response timed out", HALOW_ROLE_NAME);
            result = -1;
        }
        if (window_pending &&
            (TickType_t)(now - window_requested) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "TX window response timed out");
            result = -1;
        }
        if (result >= 0 && tx_window < 2 && !window_pending) {
            transaction_error = ESP_OK;
            if (request_window() < 0) {
                result = -1;
            }
        }
        if (result >= 0 &&
            tx_window >= 2 &&
            (TickType_t)(now - last_query) >= pdMS_TO_TICKS(STATUS_INTERVAL_MS)) {
            transaction_error = ESP_OK;
            if (query_role_status() < 0 || transaction_error != ESP_OK) {
                ESP_LOGE(TAG, "%s status query failed", HALOW_ROLE_NAME);
                result = -1;
            }
            last_query = now;
        }
        bool ready = result >= 0 && radio_ready();
        size_t length = result > 0 ? packet.length : 0;
        uint8_t source[6];
        if (length > 0) {
            memcpy(delivery_buffer, packet.frame, length);
            memcpy(source, packet.source, sizeof(source));
        }
        if (result < 0) {
            faulted = true;
            ap_interface_ready = false;
        }
        xSemaphoreGive(lock);

        /* Robusto may synchronously send a receipt here. Neither link updates
         * nor receive may execute with the vendor/SPI/state mutex held. */
        if (ready != published) {
            rob_ret_val_t status = robusto_halow_set_link_ready(ready);
            if (status != ROB_OK) {
                ESP_LOGE(TAG, "Publishing link availability failed: %d", status);
                break;
            }
            published = ready;
        }
        if (!ready && length > 0) {
            ++rx_unready_frames;
            if (rx_unready_frames == 1 || rx_unready_frames % 64U == 0) {
                ESP_LOGW(TAG, "RX SPI frame not dispatched: link not ready count=%lu bytes=%u",
                         (unsigned long)rx_unready_frames, (unsigned int)length);
            }
        }
        if (ready && length > 0) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
            bool diagnostic = frm2_sweep_receive(&sweep_rx, source, delivery_buffer,
                                                length, esp_timer_get_time());
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
            if (!diagnostic)
                frm2_sweep_select_peer(&sweep_tx, source, esp_timer_get_time());
#endif
            if (!diagnostic)
#endif
            {
                rob_ret_val_t status = robusto_halow_receive(source, delivery_buffer, length, NULL);
                if (status != ROB_OK) {
                    ESP_LOGW(TAG, "Robusto receive returned %d", status);
                }
            }
        }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
        frm2_sweep_poll(&sweep_rx, esp_timer_get_time());
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
        frm2_sweep_transmit(&sweep_tx, esp_timer_get_time());
#endif
#endif
        if (result < 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_RX
    if (sweep_rx.active && !sweep_rx.reported) frm2_sweep_report(&sweep_rx, "receiver-stopped");
#ifdef CONFIG_ROBUSTO_HALOW_SPI_FRM2_SWEEP_TX
    if (!sweep_tx.done)
        ESP_LOGE(TAG, "FRM2_SWEEP TX incomplete: receiver stopped selected=%u next_seq=%u",
                 sweep_tx.selected, sweep_tx.sequence);
#endif
#endif
    xSemaphoreTake(lock, portMAX_DELAY);
    faulted = true;
    ap_interface_ready = false;
    xSemaphoreGive(lock);
    rob_ret_val_t status = robusto_halow_set_link_ready(false);
    if (status != ROB_OK) {
        ESP_LOGE(TAG, "Clearing link availability failed: %d", status);
    }
    /* No callbacks or state accesses after signalling stop completion. */
    xSemaphoreGive(exited);
    vTaskDelete(NULL);
}

static bool hardware_allocated(void)
{
    return device != NULL || bus_initialized || cs_initialized || irq_initialized ||
           debug_uart_installed;
}

static esp_err_t initialize_transport(void)
{
    const int pins[] = {HALOW_SCLK, HALOW_MOSI, HALOW_MISO, HALOW_CS, HALOW_IRQ};
    for (unsigned int i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (pins[i] < 0 || !GPIO_IS_VALID_GPIO(pins[i])) {
            ESP_LOGE(TAG, "Invalid HaLow GPIO configuration: pin %u is %d", i, pins[i]);
            return ESP_ERR_INVALID_ARG;
        }
        for (unsigned int j = 0; j < i; ++j) {
            if (pins[i] == pins[j]) {
                ESP_LOGE(TAG, "HaLow GPIO%d is assigned to multiple signals", pins[i]);
                return ESP_ERR_INVALID_ARG;
            }
        }
        if (HALOW_DIAGNOSTICS_ENABLED && pins[i] == HALOW_DEBUG_RX) {
            ESP_LOGE(TAG, "HaLow debug UART GPIO%d conflicts with SPI/IRQ", HALOW_DEBUG_RX);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(HALOW_SCLK) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(HALOW_MOSI) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(HALOW_CS) ||
        (HALOW_DIAGNOSTICS_ENABLED &&
         (HALOW_DEBUG_RX < 0 || !GPIO_IS_VALID_GPIO(HALOW_DEBUG_RX)))) {
        ESP_LOGE(TAG, "HaLow output/debug GPIO configuration is invalid");
        return ESP_ERR_INVALID_ARG;
    }
    memset(&hgic, 0, sizeof(hgic));
    robusto_halow_spi_reset_rx_cookie();
    faulted = false;
    transaction_error = ESP_OK;
    tx_window = 2; /* Initial window specified in the pinned driver README.txt. */
    window_pending = false;
    rejected_header_logs = 0;
    rx_data_frames = 0;
    rx_unready_frames = 0;
#ifdef CONFIG_ROBUSTO_HALOW_RX_TRACE
    rx_trace_reads = 0;
#endif
    const spi_bus_config_t bus = {
        .mosi_io_num = HALOW_MOSI, .miso_io_num = HALOW_MISO,
        .sclk_io_num = HALOW_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .data4_io_num = -1, .data5_io_num = -1, .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = sizeof(spi_tx),
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    esp_err_t result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result == ESP_OK) {
        bus_initialized = true;
        if (HALOW_DIAGNOSTICS_ENABLED) {
            spi_dma_ctx_t *dma = spi_bus_get_dma_ctx(SPI2_HOST);
            if (dma == NULL || dma->dmadesc_tx == NULL) {
                ESP_LOGE(TAG, "SDSPI TX DMA descriptor unavailable");
                result = ESP_ERR_INVALID_STATE;
            } else {
                tx_descriptor = ADDR_DMA_2_CPU(dma->dmadesc_tx);
            }
        }
    }
    if (result == ESP_OK) {
        const spi_device_interface_config_t config = {
            .mode = 0, .clock_source = SPI_CLK_SRC_SPLL, .duty_cycle_pos = 128,
            .cs_ena_pretrans = 1, .cs_ena_posttrans = 1,
            .clock_speed_hz = 40000000, .spics_io_num = HALOW_CS, .queue_size = 1,
            .pre_cb = HALOW_DIAGNOSTICS_ENABLED ? capture_dma_submission : NULL,
            .post_cb = HALOW_DIAGNOSTICS_ENABLED ? capture_spi_completion : NULL,
        };
        result = spi_bus_add_device(SPI2_HOST, &config, &device);
        cs_initialized = result == ESP_OK;
    }
    if (result == ESP_OK) {
        int actual_khz;
        result = spi_device_get_actual_freq(device, &actual_khz);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "SDSPI actual clock=%d kHz", actual_khz);
            if (HALOW_DIAGNOSTICS_ENABLED) {
                ESP_LOGI(TAG, "SDSPI TX buffer=%p RX buffer=%p",
                         (void *)spi_tx, (void *)spi_rx);
            }
        }
    }
    if (result == ESP_OK) {
        /* Poll SDIO INT_PENDING independently: hgic_sdspi_init reports that
         * some modules assert IRQ only while CS is active. Do not depend on
         * a falling edge to make forward progress. */
        const gpio_config_t irq = {
            .pin_bit_mask = 1ULL << HALOW_IRQ, .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        result = gpio_config(&irq);
        irq_initialized = result == ESP_OK;
    }
    if (result == ESP_OK && HALOW_DIAGNOSTICS_ENABLED) {
        result = start_debug_capture();
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000)); /* Vendor SDIO slave initialization delay. */
        capture_debug_uart();
        memset(init_commands, 0, sizeof(init_commands));
        init_register_count = 0;
        init_register_overflow = false;
        bitrate_logs = 0;
        signal_logs = 0;
        ESP_LOGI(TAG, "Initializing SDSPI: SCLK=%d MOSI=%d MISO=%d CS=%d IRQ=%d mode=0 clock=40000000 CS=hardware-per-transfer pre=1 post=1",
                 HALOW_SCLK, HALOW_MOSI, HALOW_MISO, HALOW_CS, HALOW_IRQ);
        trace_initialization = HALOW_DIAGNOSTICS_ENABLED;
        int sdspi_result = hgic_sdspi_init(NULL);
        trace_initialization = false;
        for (unsigned int i = 0; i < init_register_count; ++i) {
            const uint8_t *tx = init_registers[i].tx;
            uint32_t address = ((uint32_t)(tx[2] & 3) << 15) |
                               ((uint32_t)tx[3] << 7) | (tx[4] >> 1);
            uint8_t status = 0xff, value = 0xff;
            bool response = robusto_halow_spi_register_response(init_registers[i].rx,
                                                             &status, &value);
            ESP_LOGI(TAG, "Init CMD52 %s F%u addr=0x%03x sent=0x%02x response=%d R5=0x%02x value=0x%02x",
                     tx[2] & 0x80 ? "write" : "read", (tx[2] >> 4) & 7,
                     (unsigned int)address, tx[5], response, status, value);
        }
        if (init_register_overflow) {
            ESP_LOGW(TAG, "Initialization CMD52 capture truncated at 32 exchanges");
        }
        if (sdspi_result < 0 || transaction_error != ESP_OK) {
            ESP_LOGE(TAG, "SDSPI initialization failed: vendor=%d SPI=%s IRQ=%d",
                     sdspi_result, esp_err_to_name(transaction_error), gpio_get_level(HALOW_IRQ));
            for (unsigned int i = 0; i < 2U; ++i) {
                if (init_commands[i].length != 0) {
                    ESP_LOGE(TAG, "CMD%u transmitted then received:", i == 0 ? 0U : 5U);
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, init_commands[i].tx, init_commands[i].length, ESP_LOG_ERROR);
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, init_commands[i].rx, init_commands[i].length, ESP_LOG_ERROR);
                }
            }
            result = transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        }
        last_interface_check = xTaskGetTickCount();
    }
    if (result == ESP_OK) {
        unsigned short vendor_id = 0, device_id = 0;
        transaction_error = ESP_OK;
        int identity = hgic_sdspi_get_devid(NULL, &vendor_id, &device_id);
        if (identity < 0 || transaction_error != ESP_OK) {
            ESP_LOGE(TAG, "SDIO identity query failed: vendor=%d SPI=%s",
                     identity, esp_err_to_name(transaction_error));
            result = transaction_error != ESP_OK ? transaction_error : ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "SDIO identity: vendor=0x%04x device=0x%04x", vendor_id, device_id);
        }
    }
    if (result == ESP_OK && HALOW_DIAGNOSTICS_ENABLED) {
        result = log_sdspi_registers();
    }
    return result;
}

static rob_ret_val_t backend_start(void *context)
{
    (void)context;
    if (!take_lock()) {
        return ROB_ERR_MUTEX;
    }
    if (running || rx_task != NULL || hardware_allocated()) {
        xSemaphoreGive(lock);
        return ROB_ERR_NOT_READY;
    }
    stopping = false;
    ap_interface_ready = false;
    esp_err_t result = ESP_OK;
#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
    result = validate_network_configuration();
#endif
    if (result == ESP_OK) result = initialize_transport();
#ifdef CONFIG_ROBUSTO_HALOW_SPI_RADIO_OFF_AT_START
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = command(disable_radio, HGIC_CMD_RADIO_ONOFF);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Startup radio-off command 65 acknowledged; RF-off not independently verified");
        } else {
            ESP_LOGE(TAG, "Startup radio-off command 65 failed; no subsequent startup commands will be sent");
        }
    }
#endif
    if (result == ESP_OK) {
        if (HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
            ESP_LOGW(TAG, "Control-write diagnostic: skipping firmware-info command 43; "
                     "radio MAC remains unavailable; raw radio startup will remain disabled");
        }
        result = check_interface(HALOW_DIAGNOSTIC_SKIP_FW_INFO
                                     ? "before command 41" : "before command 43");
    }
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = command(hgic_raw_get_fwinfo, HGIC_CMD_GET_FW_INFO);
        esp_err_t health = check_interface("after command 43");
        if (result == ESP_OK) {
            result = health;
        }
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "HaLow firmware: %u.%u.%u.%u SVN=%u",
                     (unsigned int)(hgic.fwinfo.version >> 24),
                     (unsigned int)((hgic.fwinfo.version >> 16) & 0xff),
                     (unsigned int)((hgic.fwinfo.version >> 8) & 0xff),
                     (unsigned int)(hgic.fwinfo.version & 0xff),
                     (unsigned int)hgic.fwinfo.svn_version);
            ESP_LOGI(TAG, "HaLow radio MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     hgic.addr[0], hgic.addr[1], hgic.addr[2],
                     hgic.addr[3], hgic.addr[4], hgic.addr[5]);
        } else {
            ESP_LOGE(TAG, "HaLow radio MAC unavailable: firmware-info command failed");
        }
    }
#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = apply_network_configuration();
    }
#endif
    if (result == ESP_OK) {
        result = command(set_configured_mode, HGIC_CMD_SET_WORK_MODE);
        esp_err_t health = check_interface("after command 41");
        if (result == ESP_OK) {
            result = health;
        }
    }
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = command(enable_radio, HGIC_CMD_RADIO_ONOFF);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Radio enable command 65 acknowledged; RF operation not independently verified");
        } else {
            ESP_LOGE(TAG, "Radio enable failed; startup cannot publish link readiness");
        }
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = verify_network_configuration();
    }
#endif
    if (result == ESP_OK) {
        result = command(query_role_status, HALOW_STATUS_COMMAND);
        esp_err_t health = check_interface("after role status");
        if (result == ESP_OK) {
            result = health;
        }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
#ifdef CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE
        if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
            result = verify_network_configuration();
        }
#endif
        if (result == ESP_OK) {
            result = probe_ap_interface();
            ap_interface_ready = result == ESP_OK;
        }
#endif
    }
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = configure_minimum_mcs();
    }
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ECHO_SWEEP
    if (result == ESP_OK && !HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        result = run_echo_sweep();
    }
#endif
    if (result == ESP_OK && HALOW_DIAGNOSTIC_SKIP_FW_INFO) {
        ESP_LOGE(TAG, "Control-write diagnostic completed: role/status commands responded; "
                 "radio startup intentionally blocked because firmware-info was skipped "
                 "and the radio MAC is unavailable");
        result = ESP_ERR_INVALID_STATE;
    }
    esp_err_t debug_cleanup = stop_debug_capture();
    if (result == ESP_OK) {
        result = debug_cleanup;
    }
    if (result == ESP_OK) {
        running = true;
        if (xTaskCreate(receive_task, "halow_rx", 6144, NULL, 5, &rx_task) != pdPASS) {
            running = false;
            rx_task = NULL;
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result != ESP_OK) {
        ap_interface_ready = false;
        ESP_LOGE(TAG, "Start failed: %s", esp_err_to_name(result));
        esp_err_t cleanup = release_hardware();
        if (cleanup != ESP_OK) {
            ESP_LOGE(TAG, "Failed-start cleanup failed: %s", esp_err_to_name(cleanup));
        }
    } else {
        ESP_LOGI(TAG, "%s SPI2 40MHz started; associated=%d ap_interface_ready=%u",
                 HALOW_ROLE_NAME, hgic.status.conn_state, (unsigned)ap_interface_ready);
#if !defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) && !defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
#ifdef HALOW_CONFIGURE_MINIMUM_MCS
        ESP_LOGI(TAG, "Stored network/security settings unchanged; minimum MCS applied separately");
#else
        ESP_LOGI(TAG, "Stored RF/security settings unchanged");
#endif
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_ROLE_AP
        ESP_LOGW(TAG, "AP readiness confirms mode and local raw interface, not beaconing, STA admission or RF delivery");
#endif
    }
    xSemaphoreGive(lock);
    return result == ESP_OK ? ROB_OK : ROB_ERR_INIT_FAIL;
}

static rob_ret_val_t quiesce_receiver(void)
{
    if (!take_lock()) {
        return ROB_ERR_MUTEX;
    }
    if (rx_task == xTaskGetCurrentTaskHandle()) {
        xSemaphoreGive(lock);
        return ROB_ERR_NOT_READY;
    }
    stopping = true;
    bool wait = rx_task != NULL;
    xSemaphoreGive(lock);
    if (wait && xSemaphoreTake(exited, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Receive task did not quiesce; hardware retained");
        return ROB_ERR_TIMEOUT;
    }
    if (!take_lock()) {
        /* Preserve the completion token so a subsequent stop can finish. */
        if (wait) {
            xSemaphoreGive(exited);
        }
        return ROB_ERR_MUTEX;
    }
    rx_task = NULL;
    xSemaphoreGive(lock);
    return ROB_OK;
}

static rob_ret_val_t backend_stop(void *context)
{
    (void)context;
    rob_ret_val_t quiesced = quiesce_receiver();
    if (quiesced != ROB_OK) {
        return quiesced;
    }
    if (!take_lock()) {
        return ROB_ERR_MUTEX;
    }
    esp_err_t result = release_hardware();
    if (result == ESP_OK) {
        running = false;
        ap_interface_ready = false;
    }
    xSemaphoreGive(lock);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Stop cleanup failed: %s", esp_err_to_name(result));
    }
    return result == ESP_OK ? ROB_OK : ROB_FAIL;
}

static esp_err_t reset_and_reinitialize(void)
{
    if (lock == NULL) {
        ESP_LOGE(TAG, "Reset requires a registered HaLow backend");
        return ESP_ERR_INVALID_STATE;
    }
    if (!take_lock()) {
        ESP_LOGE(TAG, "Reset state lock timed out");
        return ESP_ERR_TIMEOUT;
    }
    if ((stopping && (running || rx_task != NULL)) ||
        (!running && rx_task != NULL) ||
        rx_task == xTaskGetCurrentTaskHandle()) {
        xSemaphoreGive(lock);
        ESP_LOGE(TAG, "Reset requires an application task and no unfinished stop");
        return ESP_ERR_INVALID_STATE;
    }
#if defined(CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE) || defined(CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE)
    esp_err_t validation = validate_network_configuration();
    if (validation != ESP_OK) {
        xSemaphoreGive(lock);
        return validation;
    }
#endif
    const bool was_running = running;
    stopping = true;
    xSemaphoreGive(lock);

    ESP_LOGI(TAG, "Explicit module reset requested; quiescing traffic");
    esp_err_t result = ESP_OK;
    rob_ret_val_t status = robusto_halow_set_link_ready(false);
    if (status != ROB_OK) {
        ESP_LOGE(TAG, "Reset link shutdown failed: %d", status);
        result = status == ROB_ERR_NOT_READY ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    status = quiesce_receiver();
    if (status != ROB_OK) {
        ESP_LOGE(TAG, "Reset receive shutdown failed: %d; hardware retained", status);
        if (result != ESP_OK) {
            return result;
        }
        return status == ROB_ERR_TIMEOUT || status == ROB_ERR_MUTEX
                   ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        if (!take_lock()) {
            ESP_LOGE(TAG, "Reset submission lock timed out");
            result = ESP_ERR_TIMEOUT;
        } else {
            if (!was_running) {
                ESP_LOGI(TAG, "Preparing HaLow reset transport without startup commands");
                result = release_hardware();
                if (result == ESP_OK) {
                    result = initialize_transport();
                }
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Reset transport setup failed: %s", esp_err_to_name(result));
                }
            }
            if (result == ESP_OK) {
                result = check_interface("before reset command 132");
            }
            /* Reset may interrupt its own reply. Validate submission here,
             * then require normal startup responses after reinitialization. */
            if (result == ESP_OK) {
                result = submit_command(hgic_raw_mcu_reset, HGIC_CMD_RESET);
            }
            xSemaphoreGive(lock);
        }
    }
    /* Failed startup leaves Robusto stopped; only its transport is allocated
     * above. A running backend must also transition Robusto's lifecycle. */
    status = was_running ? robusto_halow_stop() : backend_stop(NULL);
    if (status != ROB_OK) {
        ESP_LOGE(TAG, "Reset stop/cleanup failed: %d", status);
        if (result == ESP_OK) {
            result = ESP_FAIL;
        }
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Reset failed: %s; no reinitialization attempted",
                 esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "Module reboot command submitted; reinitializing once; "
             "reboot itself is not independently confirmed");
    status = robusto_halow_start();
    if (status != ROB_OK) {
        ESP_LOGE(TAG, "Reset reinitialization failed: %d", status);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Explicit reset/reinitialization completed; association reported separately");
    return ESP_OK;
}

static int backend_write(void *context, const uint8_t destination[6],
                         const uint8_t *frame, uint32_t length)
{
    (void)context;
    if (destination == NULL || frame == NULL || length == 0 ||
        length > ROBUSTO_HALOW_FRAME_MAX) {
        return ROB_ERR_INVALID_ARG;
    }
    if (!take_lock()) {
        return ROB_ERR_MUTEX;
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    tx_trace_record_t *trace = tx_trace_frame(destination, frame, length);
#endif
    if (!running || stopping || faulted || !radio_ready()) {
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        if (trace != NULL) {
            trace->backend_result = ROB_ERR_NOT_READY;
            trace->window_after = tx_window;
        }
#endif
        xSemaphoreGive(lock);
        return ROB_ERR_NOT_READY;
    }
    if (tx_window < 2) {
        /* Refresh promptly, but never wait on this task's own RX path. */
        transaction_error = ESP_OK;
        if (!window_pending && request_window() < 0) {
            faulted = true;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
            if (trace != NULL) {
                trace->backend_result = ROB_ERR_SEND_FAIL;
                trace->spi_result = transaction_error;
                trace->window_after = tx_window;
            }
#endif
            xSemaphoreGive(lock);
            return ROB_ERR_SEND_FAIL;
        }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
        if (trace != NULL) {
            ++trace->busy;
            trace->backend_result = ROB_ERR_QUEUE_FULL;
            trace->window_after = tx_window;
        }
#endif
        xSemaphoreGive(lock);
        return ROB_ERR_QUEUE_FULL;
    }
    transaction_error = ESP_OK;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (trace != NULL) {
        trace->vendor_called = true;
        trace->vendor_bytes = length + 22U;
        tx_trace_current = trace;
    }
#endif
    int result = hgic_raw_send((uint8_t *)destination, (uint8_t *)frame, length);
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    tx_trace_current = NULL;
    if (trace != NULL) {
        trace->vendor_result = result;
        if (trace->spi_result == ESP_OK) trace->spi_result = transaction_error;
        trace->window_after = tx_window;
    }
#endif
    if (transaction_error != ESP_OK || result != (int)(length + 22U)) {
        ESP_LOGE(TAG, "Local raw write failed: expected=%lu actual=%d SPI=%s",
                 (unsigned long)(length + 22U), result, esp_err_to_name(transaction_error));
        faulted = true;
        result = ROB_ERR_SEND_FAIL;
    } else {
        result = length;
    }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    if (trace != NULL) trace->backend_result = result;
#endif
    xSemaphoreGive(lock);
    return result;
}

static rob_ret_val_t backend_reset_and_reinitialize(void *context)
{
    (void)context;
    esp_err_t result = reset_and_reinitialize();
    switch (result) {
    case ESP_OK:
        return ROB_OK;
    case ESP_ERR_TIMEOUT:
        return ROB_ERR_TIMEOUT;
    case ESP_ERR_INVALID_STATE:
        return ROB_ERR_NOT_READY;
    case ESP_ERR_INVALID_ARG:
        return ROB_ERR_INVALID_ARG;
    case ESP_ERR_NO_MEM:
        return ROB_ERR_OUT_OF_MEMORY;
    default:
        return ROB_ERR_INIT_FAIL;
    }
}

static rob_ret_val_t backend_get_rx_stats(void *context, robusto_halow_rx_stats_t *out)
{
    (void)context;
    if (!take_lock()) {
        ESP_LOGE(TAG, "RX statistics snapshot mutex timed out");
        return ROB_ERR_MUTEX;
    }
    out->spi_frames = rx_data_frames;
    out->spi_unready = rx_unready_frames;
    xSemaphoreGive(lock);
    return ROB_OK;
}

rob_ret_val_t robusto_halow_spi_register(void)
{
    if (lock != NULL) {
        ESP_LOGE(TAG, "SPI driver is already registered");
        return ROB_ERR_NOT_READY;
    }
    lock = xSemaphoreCreateMutex();
    exited = xSemaphoreCreateBinary();
    if (lock == NULL || exited == NULL) {
        if (lock != NULL) {
            vSemaphoreDelete(lock);
        }
        if (exited != NULL) {
            vSemaphoreDelete(exited);
        }
        lock = NULL;
        exited = NULL;
        ESP_LOGE(TAG, "SPI driver synchronization allocation failed");
        return ROB_ERR_OUT_OF_MEMORY;
    }
    const robusto_halow_driver_t binding = {
        .start = backend_start, .stop = backend_stop, .write = backend_write,
        .reset_and_reinitialize = backend_reset_and_reinitialize,
        .get_rx_stats = backend_get_rx_stats,
    };
    rob_ret_val_t result = robusto_halow_register_driver(&binding);
    if (result != ROB_OK) {
        ESP_LOGE(TAG, "Driver registration failed: %d", result);
        vSemaphoreDelete(lock);
        vSemaphoreDelete(exited);
        lock = NULL;
        exited = NULL;
        return result;
    }
    return ROB_OK;
}
#endif
