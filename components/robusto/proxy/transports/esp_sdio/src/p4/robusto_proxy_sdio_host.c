#include "robusto_proxy_sdio_host.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"

#include "robusto_rsd1_protocol.h"

#define PROXY_SDIO_SLOT SDMMC_HOST_SLOT_1
#define PROXY_SDIO_RESET_DELAY_MS 1500U
#define PROXY_SDIO_CARD_INIT_RETRIES 15U
#define PROXY_SDIO_CARD_INIT_RETRY_MS 100U
#define PROXY_SDIO_READY_POLL_MS 10U

typedef struct robusto_proxy_sdio_host_state {
    sdmmc_card_t card;
    essl_handle_t link;
    uint32_t next_sequence;
    size_t buffered_receive_size;
    bool card_initialized;
    bool host_initialized;
    bool receive_interrupt_registered;
} robusto_proxy_sdio_host_state_t;

static robusto_proxy_sdio_host_state_t state;
static StaticSemaphore_t receive_interrupt_storage;
static SemaphoreHandle_t receive_interrupt;
static DMA_ATTR __attribute__((aligned(CONFIG_CACHE_L1_CACHE_LINE_SIZE)))
    uint8_t send_packet[ROBUSTO_RSD1_MAX_PACKET_SIZE];
static DMA_ATTR __attribute__((aligned(CONFIG_CACHE_L1_CACHE_LINE_SIZE)))
    uint8_t receive_packet[ROBUSTO_RSD1_MAX_PACKET_SIZE];
static const char *TAG = "robusto_rsd1";

static void receive_interrupt_handler(void *argument)
{
    BaseType_t task_woken = pdFALSE;

    (void)argument;
    xSemaphoreGiveFromISR(receive_interrupt, &task_woken);
    portYIELD_FROM_ISR(task_woken);
}

static esp_err_t initialize_receive_interrupt(void)
{
    esp_err_t error;

    if (receive_interrupt == NULL) {
        receive_interrupt =
            xSemaphoreCreateBinaryStatic(&receive_interrupt_storage);
        if (receive_interrupt == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    error = gpio_install_isr_service(0);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    error = gpio_set_intr_type(CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO,
                               GPIO_INTR_NEGEDGE);
    if (error != ESP_OK) {
        return error;
    }
    error = gpio_isr_handler_add(CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO,
                                 receive_interrupt_handler, NULL);
    if (error != ESP_OK) {
        return error;
    }
    state.receive_interrupt_registered = true;
    error = gpio_intr_enable(CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO);
    if (error != ESP_OK) {
        esp_err_t cleanup_error =
            gpio_isr_handler_remove(CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO);
        state.receive_interrupt_registered = false;
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(TAG, "Receive interrupt cleanup: %s",
                     esp_err_to_name(cleanup_error));
        }
        return error;
    }
    return ESP_OK;
}

static esp_err_t wait_for_receive_interrupt(uint32_t timeout_ms)
{
    xSemaphoreTake(receive_interrupt, 0);
    if (gpio_get_level(CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO) == 0) {
        return ESP_OK;
    }
    if (xSemaphoreTake(receive_interrupt, pdMS_TO_TICKS(timeout_ms)) ==
        pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static void advance_sequence(void)
{
    ++state.next_sequence;
    if (state.next_sequence == 0U) {
        state.next_sequence = 1U;
    }
}

static esp_err_t reset_c6(void)
{
    gpio_config_t reset_config = {
        .pin_bit_mask =
            1ULL << CONFIG_ROBUSTO_PROXY_SDIO_P4_C6_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&reset_config);

    if (error != ESP_OK) {
        return error;
    }
    error = gpio_set_level(CONFIG_ROBUSTO_PROXY_SDIO_P4_C6_RESET_GPIO, 1);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    error = gpio_set_level(CONFIG_ROBUSTO_PROXY_SDIO_P4_C6_RESET_GPIO, 0);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    error = gpio_set_level(CONFIG_ROBUSTO_PROXY_SDIO_P4_C6_RESET_GPIO, 1);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(PROXY_SDIO_RESET_DELAY_MS));
    return ESP_OK;
}

static esp_err_t initialize_card(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    esp_err_t error = ESP_FAIL;

    memset(&state.card, 0, sizeof(state.card));
    state.card_initialized = false;
    host.slot = PROXY_SDIO_SLOT;
    host.max_freq_khz = CONFIG_ROBUSTO_PROXY_SDIO_P4_CLOCK_KHZ;
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;

    for (uint32_t attempt = 0U; attempt < PROXY_SDIO_CARD_INIT_RETRIES;
         ++attempt) {
        error = sdmmc_card_init(&host, &state.card);
        if (error == ESP_OK) {
            state.card_initialized = true;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(PROXY_SDIO_CARD_INIT_RETRY_MS));
    }
    return error;
}

static esp_err_t initialize_link(essl_handle_t link, uint32_t ready_timeout_ms)
{
    TickType_t start_tick;
    uint8_t io_enable = 0;
    uint8_t io_ready = 0;
    uint8_t interrupt_enable = 0;
    uint8_t bus_width = 0;
    uint16_t block_size = 512;
    uint16_t block_size_read = 0;
    uint8_t *block_size_read_bytes = (uint8_t *)&block_size_read;
    const uint8_t *block_size_bytes = (const uint8_t *)&block_size;
    size_t function_1_offset = SD_IO_FBR_START;
    esp_err_t error;

    if (link == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state.link != link || !state.host_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    error = sdmmc_io_read_byte(&state.card, 0, SD_IO_CCCR_FN_ENABLE, &io_enable);
    if (error != ESP_OK) {
        return error;
    }
    error = sdmmc_io_read_byte(&state.card, 0, SD_IO_CCCR_FN_READY, &io_ready);
    if (error != ESP_OK) {
        return error;
    }

    io_enable |= BIT(1);
    error = sdmmc_io_write_byte(&state.card, 0, SD_IO_CCCR_FN_ENABLE, io_enable,
                                &io_enable);
    if (error != ESP_OK) {
        return error;
    }

    start_tick = xTaskGetTickCount();
    while ((io_ready & BIT(1)) == 0U) {
        error = sdmmc_io_read_byte(&state.card, 0, SD_IO_CCCR_FN_READY, &io_ready);
        if (error != ESP_OK) {
            return error;
        }
        if ((io_ready & BIT(1)) != 0U) {
            break;
        }
        if (pdTICKS_TO_MS(xTaskGetTickCount() - start_tick) >=
            ready_timeout_ms) {
            ESP_LOGW(TAG,
                     "Host init: SDIO function 1 never became ready within %lu ms (IOR=0x%02x)",
                     (unsigned long)ready_timeout_ms,
                     (unsigned int)io_ready);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(PROXY_SDIO_READY_POLL_MS));
    }

    error = sdmmc_io_read_byte(&state.card, 0, SD_IO_CCCR_INT_ENABLE,
                               &interrupt_enable);
    if (error != ESP_OK) {
        return error;
    }
    interrupt_enable |= BIT(0) | BIT(1);
    error = sdmmc_io_write_byte(&state.card, 0, SD_IO_CCCR_INT_ENABLE,
                                interrupt_enable, &interrupt_enable);
    if (error != ESP_OK) {
        return error;
    }

    error = sdmmc_io_read_byte(&state.card, 0, SD_IO_CCCR_BUS_WIDTH, &bus_width);
    if (error != ESP_OK) {
        return error;
    }
    bus_width |= CCCR_BUS_WIDTH_ECSI;
    error = sdmmc_io_write_byte(&state.card, 0, SD_IO_CCCR_BUS_WIDTH, bus_width,
                                &bus_width);
    if (error != ESP_OK) {
        return error;
    }

    error = sdmmc_io_write_byte(&state.card, 0, SD_IO_CCCR_BLKSIZEL,
                                block_size_bytes[0], NULL);
    if (error != ESP_OK) {
        return error;
    }
    error = sdmmc_io_write_byte(&state.card, 0, SD_IO_CCCR_BLKSIZEH,
                                block_size_bytes[1], NULL);
    if (error != ESP_OK) {
        return error;
    }

    error = sdmmc_io_write_byte(&state.card, 0,
                                function_1_offset + SD_IO_CCCR_BLKSIZEL,
                                block_size_bytes[0], NULL);
    if (error != ESP_OK) {
        return error;
    }
    error = sdmmc_io_write_byte(&state.card, 0,
                                function_1_offset + SD_IO_CCCR_BLKSIZEH,
                                block_size_bytes[1], NULL);
    if (error != ESP_OK) {
        return error;
    }
    error = sdmmc_io_read_byte(&state.card, 0,
                               function_1_offset + SD_IO_CCCR_BLKSIZEL,
                               &block_size_read_bytes[0]);
    if (error != ESP_OK) {
        return error;
    }
    error = sdmmc_io_read_byte(&state.card, 0,
                               function_1_offset + SD_IO_CCCR_BLKSIZEH,
                               &block_size_read_bytes[1]);
    if (error != ESP_OK) {
        return error;
    }
    if (block_size_read != block_size) {
        ESP_LOGW(TAG,
                 "Host init: function 1 block size read back as %u instead of %u",
                 (unsigned int)block_size_read,
                 (unsigned int)block_size);
    }

    return ESP_OK;
}

static esp_err_t initialize_host(bool reset_before_connect)
{
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    essl_sdio_config_t link_config;
    esp_err_t error;

    if (state.link != NULL || state.host_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (reset_before_connect) {
        ESP_LOGI(TAG, "Host init: resetting C6 before SDIO connect");
        error = reset_c6();
        if (error != ESP_OK) {
            return error;
        }
    }
    ESP_LOGI(TAG, "Host init: sdmmc_host_init()");
    error = sdmmc_host_init();
    if (error != ESP_OK) {
        return error;
    }
    state.host_initialized = true;

    slot.width = 1;
    slot.clk = CONFIG_ROBUSTO_PROXY_SDIO_P4_CLK_GPIO;
    slot.cmd = CONFIG_ROBUSTO_PROXY_SDIO_P4_CMD_GPIO;
    slot.d0 = CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT0_GPIO;
    slot.d1 = CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO;
    slot.d2 = -1;
    slot.d3 = -1;
    ESP_LOGI(TAG, "Host init: sdmmc_host_init_slot()");
    error = sdmmc_host_init_slot(PROXY_SDIO_SLOT, &slot);
    if (error != ESP_OK) {
        goto cleanup;
    }
    ESP_LOGI(TAG, "Host init: initialize_card()");
    error = initialize_card();
    if (error != ESP_OK) {
        goto cleanup;
    }

    link_config.card = &state.card;
    link_config.recv_buffer_size = ROBUSTO_RSD1_MAX_PACKET_SIZE;
    ESP_LOGI(TAG, "Host init: essl_sdio_init_dev()");
    error = essl_sdio_init_dev(&state.link, &link_config);
    if (error != ESP_OK) {
        goto cleanup;
    }
    ESP_LOGI(TAG, "Host init: essl_init()");
    error = initialize_link(state.link, PROXY_SDIO_RESET_DELAY_MS);
    if (error != ESP_OK) {
        goto cleanup;
    }
    ESP_LOGI(TAG, "Host init: initialize_receive_interrupt()");
    error = initialize_receive_interrupt();
    if (error != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(TAG, "Host init: ready");
    state.next_sequence = 1U;
    return ESP_OK;

cleanup:
    esp_err_t cleanup_error = robusto_proxy_sdio_host_deinit();
    if (cleanup_error != ESP_OK) {
        ESP_LOGE(TAG, "SDIO initialization cleanup: %s",
                 esp_err_to_name(cleanup_error));
    }
    return error;
}

esp_err_t robusto_proxy_sdio_host_init(void)
{
    return initialize_host(true);
}

esp_err_t robusto_proxy_sdio_host_init_without_reset(void)
{
    return initialize_host(false);
}

esp_err_t robusto_proxy_sdio_host_send(uint32_t message_id,
                                    const uint8_t *payload,
                                    size_t payload_size,
                                    uint32_t timeout_ms)
{
    size_t packet_size = 0U;
    robusto_rsd1_result_t result;
    esp_err_t error;

    if (state.link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_size > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    result = robusto_rsd1_encode(
        send_packet, sizeof(send_packet), message_id, state.next_sequence,
        payload, (uint16_t)payload_size, &packet_size);
    if (result == ROBUSTO_RSD1_INVALID_ARGUMENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (result != ROBUSTO_RSD1_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    error = essl_send_packet(state.link, send_packet, packet_size, timeout_ms);
    if (error == ESP_OK) {
        advance_sequence();
    }
    return error;
}

esp_err_t robusto_proxy_sdio_host_receive(uint32_t *message_id,
                                       uint8_t *payload,
                                       size_t payload_capacity,
                                       size_t *payload_size,
                                       uint32_t timeout_ms)
{
    robusto_rsd1_packet_view_t packet;
    size_t packet_size = 0U;
    uint32_t interrupt_raw = 0U;
    uint32_t interrupt_status = 0U;
    size_t received_size = sizeof(receive_packet);
    esp_err_t error;

    if (state.link == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (message_id == NULL || payload_size == NULL ||
        (payload_capacity > 0U && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (state.buffered_receive_size == 0U) {
        error = wait_for_receive_interrupt(timeout_ms);
        if (error != ESP_OK) {
            if (error != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "SDIO receive wait interrupt: %s",
                         esp_err_to_name(error));
            }
            return error;
        }
        error = essl_get_intr(state.link, &interrupt_raw, &interrupt_status,
                              timeout_ms);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SDIO receive read interrupt: %s",
                     esp_err_to_name(error));
            return error;
        }
        error = essl_clear_intr(state.link, interrupt_raw, timeout_ms);
        if (error != ESP_OK) {
            ESP_LOGE(TAG,
                     "SDIO receive clear interrupt raw=0x%08lx status=0x%08lx: %s",
                     (unsigned long)interrupt_raw,
                     (unsigned long)interrupt_status, esp_err_to_name(error));
            return error;
        }
        if ((interrupt_status & ESSL_SDIO_DEF_ESP32C6.new_packet_intr_mask) ==
            0U) {
            return ESP_ERR_NOT_FOUND;
        }

        error = essl_get_packet(state.link, receive_packet,
                                sizeof(receive_packet), &received_size,
                                timeout_ms);
        if (error == ESP_ERR_NOT_FINISHED) {
            esp_err_t cleanup_error = robusto_proxy_sdio_host_deinit();
            if (cleanup_error != ESP_OK) {
                ESP_LOGE(TAG, "SDIO oversized-packet cleanup: %s",
                         esp_err_to_name(cleanup_error));
                return cleanup_error;
            }
            return ESP_ERR_INVALID_SIZE;
        }
        if (error != ESP_OK) {
            ESP_LOGE(TAG,
                     "SDIO receive packet raw=0x%08lx status=0x%08lx size=%u: %s",
                     (unsigned long)interrupt_raw,
                     (unsigned long)interrupt_status,
                     (unsigned int)received_size, esp_err_to_name(error));
            return error;
        }
        state.buffered_receive_size = received_size;
    }
    robusto_rsd1_result_t decode_result = robusto_rsd1_decode_prefix(
        receive_packet, state.buffered_receive_size, &packet, &packet_size);
    if (decode_result != ROBUSTO_RSD1_OK) {
        if (state.buffered_receive_size >= 8U) {
            ESP_LOGE(TAG,
                     "RSD1 decode failed result=%u size=%u head=%02x %02x %02x %02x %02x %02x %02x %02x",
                     (unsigned int)decode_result,
                     (unsigned int)state.buffered_receive_size,
                     receive_packet[0],
                     receive_packet[1], receive_packet[2], receive_packet[3],
                     receive_packet[4], receive_packet[5], receive_packet[6],
                     receive_packet[7]);
        } else {
            ESP_LOGE(TAG, "RSD1 decode failed result=%u size=%u",
                     (unsigned int)decode_result,
                     (unsigned int)state.buffered_receive_size);
        }
        state.buffered_receive_size = 0U;
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (packet.payload_size > payload_capacity) {
        state.buffered_receive_size = 0U;
        return ESP_ERR_INVALID_SIZE;
    }

    *message_id = packet.message_id;
    *payload_size = packet.payload_size;
    if (packet.payload_size > 0U) {
        memcpy(payload, packet.payload, packet.payload_size);
    }
    state.buffered_receive_size -= packet_size;
    if (state.buffered_receive_size > 0U) {
        memmove(receive_packet, receive_packet + packet_size,
                state.buffered_receive_size);
    }
    return ESP_OK;
}

esp_err_t robusto_proxy_sdio_host_deinit(void)
{
    esp_err_t error = ESP_OK;

    if (state.receive_interrupt_registered) {
        esp_err_t interrupt_error = gpio_intr_disable(
            CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO);
        esp_err_t remove_error = gpio_isr_handler_remove(
            CONFIG_ROBUSTO_PROXY_SDIO_P4_DAT1_GPIO);
        state.receive_interrupt_registered = false;
        if (interrupt_error != ESP_OK) {
            error = interrupt_error;
        } else if (remove_error != ESP_OK) {
            error = remove_error;
        }
    }
    if (state.link != NULL) {
        esp_err_t link_error = essl_sdio_deinit_dev(state.link);
        state.link = NULL;
        if (error == ESP_OK) {
            error = link_error;
        }
    }
    if (state.card_initialized && state.card.host.dma_aligned_buffer != NULL) {
        free(state.card.host.dma_aligned_buffer);
        state.card.host.dma_aligned_buffer = NULL;
    }
    if (state.host_initialized) {
        esp_err_t host_error;

        host_error = sdmmc_host_deinit();
        state.host_initialized = false;
        state.card_initialized = false;
        if (error == ESP_OK) {
            error = host_error;
        }
    }
    memset(&state.card, 0, sizeof(state.card));
    state.next_sequence = 0U;
    state.buffered_receive_size = 0U;
    return error;
}