#include "robusto_proxy_sdio_device.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/sdio_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "robusto_rsd1_protocol.h"
#include "robusto_message_frontend.h"

#define RX_BUFFER_COUNT 8U
#define TX_BUFFER_COUNT 8U
#define CALLBACK_COUNT 3U
#define SEND_TIMEOUT_MS 500U
#define RECEIVE_TASK_STACK_SIZE 6144U

typedef struct {
    uint32_t message_id;
    robusto_message_receive_fn receive;
} frontend_callback_t;

typedef struct {
    uint8_t bytes[ROBUSTO_PROXY_SDIO_SLAVE_MAX_PACKET_SIZE];
    bool busy;
    uint8_t alignment_padding[3];
} tx_slot_t;

static const char *TAG = "robusto_rsd1";
static DMA_ATTR uint8_t rx_buffers[RX_BUFFER_COUNT][ROBUSTO_RSD1_MAX_PACKET_SIZE];
static DMA_ATTR tx_slot_t tx_slots[TX_BUFFER_COUNT];
static frontend_callback_t callbacks[CALLBACK_COUNT];
static SemaphoreHandle_t frontend_mutex;
static bool frontend_started;
static uint32_t next_sequence = 1U;

static void reclaim_tx_slots(void)
{
    void *argument = NULL;

    while (sdio_slave_send_get_finished(&argument, 0) == ESP_OK) {
        tx_slot_t *slot = (tx_slot_t *)argument;
        slot->busy = false;
    }
}

static tx_slot_t *acquire_tx_slot(TickType_t timeout)
{
    TickType_t start = xTaskGetTickCount();

    do {
        reclaim_tx_slots();
        for (size_t index = 0; index < TX_BUFFER_COUNT; ++index) {
            if (!tx_slots[index].busy) {
                tx_slots[index].busy = true;
                return &tx_slots[index];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((xTaskGetTickCount() - start) < timeout);

    return NULL;
}

esp_err_t robusto_message_frontend_register(uint32_t message_id,
                                            robusto_message_receive_fn receive)
{
    size_t empty_index = CALLBACK_COUNT;

    if (message_id == 0U || frontend_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(frontend_mutex, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t index = 0; index < CALLBACK_COUNT; ++index) {
        if (callbacks[index].message_id == message_id) {
            callbacks[index].receive = receive;
            if (receive == NULL) {
                callbacks[index].message_id = 0U;
            }
            xSemaphoreGive(frontend_mutex);
            return ESP_OK;
        }
        if (empty_index == CALLBACK_COUNT && callbacks[index].message_id == 0U) {
            empty_index = index;
        }
    }
    if (receive == NULL) {
        xSemaphoreGive(frontend_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    if (empty_index == CALLBACK_COUNT) {
        xSemaphoreGive(frontend_mutex);
        return ESP_ERR_NO_MEM;
    }
    callbacks[empty_index].message_id = message_id;
    callbacks[empty_index].receive = receive;
    xSemaphoreGive(frontend_mutex);
    return ESP_OK;
}

esp_err_t robusto_message_frontend_send(uint32_t message_id,
                                        const uint8_t *data,
                                        size_t data_len)
{
    size_t packet_size = 0U;
    tx_slot_t *slot;
    esp_err_t error;

    if (message_id == 0U || (data_len > 0U && data == NULL) ||
        data_len > ROBUSTO_PROXY_SDIO_SLAVE_MAX_FRONTEND_PAYLOAD_SIZE ||
        frontend_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(frontend_mutex, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    slot = acquire_tx_slot(pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (slot == NULL) {
        xSemaphoreGive(frontend_mutex);
        return ESP_ERR_TIMEOUT;
    }
    robusto_rsd1_result_t result = robusto_rsd1_encode(
        slot->bytes, sizeof(slot->bytes), message_id, next_sequence,
        data, (uint16_t)data_len, &packet_size);
    if (result != ROBUSTO_RSD1_OK) {
        slot->busy = false;
        xSemaphoreGive(frontend_mutex);
        return ESP_ERR_INVALID_SIZE;
    }
    error = sdio_slave_send_queue(slot->bytes, packet_size, slot,
                                  pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (error == ESP_OK) {
        ++next_sequence;
        if (next_sequence == 0U) {
            next_sequence = 1U;
        }
    } else {
        slot->busy = false;
    }
    xSemaphoreGive(frontend_mutex);
    return error;
}

static robusto_message_receive_fn find_receive(uint32_t message_id)
{
    robusto_message_receive_fn receive = NULL;

    if (xSemaphoreTake(frontend_mutex, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) {
        return NULL;
    }
    for (size_t index = 0; index < CALLBACK_COUNT; ++index) {
        if (callbacks[index].message_id == message_id) {
            receive = callbacks[index].receive;
            break;
        }
    }
    xSemaphoreGive(frontend_mutex);
    return receive;
}

static void receive_task_main(void *context)
{
    (void)context;
    for (;;) {
        sdio_slave_buf_handle_t handle = NULL;
        esp_err_t error = sdio_slave_recv_packet(&handle, portMAX_DELAY);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Receive packet: %s", esp_err_to_name(error));
            continue;
        }

        size_t packet_size = 0U;
        uint8_t *bytes = sdio_slave_recv_get_buf(handle, &packet_size);
        robusto_rsd1_packet_view_t packet;
        robusto_rsd1_result_t result = robusto_rsd1_decode(
            bytes, packet_size, &packet);
        if (result != ROBUSTO_RSD1_OK) {
            ESP_LOGE(TAG, "Reject packet: result=%d size=%u",
                     result, (unsigned int)packet_size);
        } else {
            robusto_message_receive_fn receive = find_receive(packet.message_id);
            if (receive == NULL) {
                ESP_LOGE(TAG, "No handler for message=0x%08lx",
                         (unsigned long)packet.message_id);
            } else {
                receive(packet.message_id, packet.payload, packet.payload_size);
            }
        }
        error = sdio_slave_recv_load_buf(handle);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Reload receive buffer: %s",
                     esp_err_to_name(error));
            ESP_ERROR_CHECK(error);
        }
    }
}

esp_err_t robusto_proxy_sdio_device_init(void)
{
    sdio_slave_config_t config = {
        .sending_mode = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size = TX_BUFFER_COUNT,
        .recv_buffer_size = ROBUSTO_RSD1_MAX_PACKET_SIZE,
        .event_cb = NULL,
        .flags = SDIO_SLAVE_FLAG_DAT2_DISABLED,
    };

    if (frontend_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    frontend_mutex = xSemaphoreCreateMutex();
    if (frontend_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = sdio_slave_initialize(&config);
    if (error != ESP_OK) {
        vSemaphoreDelete(frontend_mutex);
        frontend_mutex = NULL;
        return error;
    }
    for (size_t index = 0; index < RX_BUFFER_COUNT; ++index) {
        sdio_slave_buf_handle_t handle = sdio_slave_recv_register_buf(rx_buffers[index]);
        if (handle == NULL) {
            sdio_slave_deinit();
            vSemaphoreDelete(frontend_mutex);
            frontend_mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
        error = sdio_slave_recv_load_buf(handle);
        if (error != ESP_OK) {
            sdio_slave_deinit();
            vSemaphoreDelete(frontend_mutex);
            frontend_mutex = NULL;
            return error;
        }
    }
    sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET);
    return ESP_OK;
}

esp_err_t robusto_proxy_sdio_device_start(void)
{
    if (frontend_mutex == NULL || frontend_started) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = sdio_slave_start();
    if (error != ESP_OK) {
        return error;
    }
    if (xTaskCreate(receive_task_main, "robusto_rsd1_rx",
                    RECEIVE_TASK_STACK_SIZE, NULL, 6, NULL) != pdPASS) {
        sdio_slave_stop();
        return ESP_ERR_NO_MEM;
    }
    frontend_started = true;
    ESP_LOGI(TAG, "Raw SDIO frontend initialized");
    return ESP_OK;
}