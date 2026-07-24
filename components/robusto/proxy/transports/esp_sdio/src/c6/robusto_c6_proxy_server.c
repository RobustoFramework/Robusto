#include "robusto_c6_proxy_server.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "robusto_proxy_sdio_messages.h"
#include "robusto_c6_pubsub_backend.h"
#include "robusto_message_frontend.h"
#include "robusto_proxy_protocol.h"
#include "robusto_proxy_service.h"

static const char *TAG = "robusto_c6_proxy_server";
static QueueHandle_t frame_queue;
static TaskHandle_t bridge_task;
static robusto_proxy_service_t proxy_service;
static robusto_c6_pubsub_backend_t pubsub_backend;
static robusto_proxy_pubsub_server_adapter_t pubsub_adapter;
static robusto_proxy_pubsub_subscription_t pubsub_subscriptions[16];
static uint8_t pubsub_event_pool[ROBUSTO_PROXY_PUBSUB_MAX_DELIVERY_DATA_BYTES];

typedef struct {
    size_t size;
    uint8_t bytes[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
} proxy_frame_item_t;

static proxy_frame_item_t callback_frame;
static proxy_frame_item_t worker_frame;
static uint8_t worker_response[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
static uint8_t delivery_payload[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
static uint8_t delivery_event[ROBUSTO_PROXY_SLOT_SIZE_BYTES];

static void frame_request_callback(uint32_t msg_id,
                                   const uint8_t *data,
                                   size_t data_len)
{
    if (msg_id != ROBUSTO_PROXY_SDIO_REQUEST_MSG_ID || data == NULL ||
        data_len > sizeof(callback_frame.bytes)) {
        ESP_LOGE(TAG, "Invalid frame bridge envelope");
        return;
    }
    callback_frame.size = data_len;
    memcpy(callback_frame.bytes, data, data_len);
    if (xQueueSend(frame_queue, &callback_frame, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Bounded frame queue is full");
    }
}

static void send_pending_deliveries(void)
{
    uint8_t opcode;
    size_t payload_size;
    size_t event_size;

    while (robusto_proxy_pubsub_server_adapter_take_delivery(
        &pubsub_adapter,
        (proxy_service.session.enabled_features &
         ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_DELIVERY) != 0U,
        &opcode, delivery_payload, sizeof(delivery_payload),
        &payload_size)) {
        robusto_proxy_result_t result =
            robusto_proxy_service_build_pubsub_event(
                &proxy_service, opcode, delivery_payload, payload_size,
                delivery_event, sizeof(delivery_event), &event_size);
        if (result != ROBUSTO_PROXY_RESULT_OK) {
            ESP_LOGE(TAG, "Build delivery event result=%d", result);
            proxy_service.errors += 1U;
            continue;
        }
        esp_err_t error = robusto_message_frontend_send(
            ROBUSTO_PROXY_SDIO_EVENT_MSG_ID, delivery_event, event_size);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Send delivery event: %s",
                     esp_err_to_name(error));
            proxy_service.errors += 1U;
            break;
        }
    }
}

static void bridge_task_main(void *context)
{
    size_t response_size;

    (void)context;
    for (;;) {
        if (xQueueReceive(frame_queue, &worker_frame, pdMS_TO_TICKS(20)) == pdTRUE) {
            response_size = 0;
            robusto_proxy_result_t result = robusto_proxy_service_handle_frame(
                &proxy_service,
            worker_frame.bytes,
            worker_frame.size,
                (uint32_t)(esp_timer_get_time() / 1000),
            worker_response,
            sizeof(worker_response),
                &response_size);
            if (result == ROBUSTO_PROXY_RESULT_OK) {
                esp_err_t error = robusto_message_frontend_send(
                    ROBUSTO_PROXY_SDIO_RESPONSE_MSG_ID,
                    worker_response,
                    response_size);
                if (error != ESP_OK) {
                    ESP_LOGE(TAG, "Send logical response: %s",
                             esp_err_to_name(error));
                }
            } else {
                if (result == ROBUSTO_PROXY_RESULT_BAD_CRC) {
                    ++proxy_service.rx_crc_errors;
                }
                ESP_LOGW(TAG, "Drop logical frame result=%d", result);
            }
        }
        send_pending_deliveries();
        (void)robusto_proxy_service_tick(&proxy_service,
                                         (uint32_t)(esp_timer_get_time() / 1000));
    }
}

esp_err_t robusto_c6_proxy_server_init(void)
{
    uint64_t boot_id = ((uint64_t)esp_random() << 32) | esp_random();
    if (boot_id == 0U) {
        boot_id = 1U;
    }
    robusto_proxy_service_init(&proxy_service,
                               ROBUSTO_PROXY_PROFILE_LOW_MEMORY,
                               boot_id,
                               1U,
                               1U,
                               2U,
                               (uint32_t)(esp_timer_get_time() / 1000));
    if (!robusto_c6_pubsub_backend_init(
            &pubsub_backend, &pubsub_adapter, pubsub_subscriptions,
            sizeof(pubsub_subscriptions) / sizeof(pubsub_subscriptions[0]),
            pubsub_event_pool, sizeof(pubsub_event_pool))) {
        ESP_LOGE(TAG, "Initialize Robusto PubSub backend");
        return ESP_ERR_NO_MEM;
    }
    robusto_proxy_service_set_pubsub_adapter(
        &proxy_service,
        robusto_proxy_pubsub_server_adapter_operations(),
        &pubsub_adapter);

    frame_queue = xQueueCreate(ROBUSTO_PROXY_SDIO_QUEUE_CAPACITY,
                               sizeof(proxy_frame_item_t));
    if (frame_queue == NULL) {
        robusto_c6_pubsub_backend_deinit(&pubsub_backend);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = robusto_message_frontend_register(
        ROBUSTO_PROXY_SDIO_REQUEST_MSG_ID,
        frame_request_callback);
    if (error != ESP_OK) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        robusto_c6_pubsub_backend_deinit(&pubsub_backend);
        return error;
    }
    ESP_LOGI(TAG, "Logical frame bridge registered");
    return ESP_OK;
}

esp_err_t robusto_c6_proxy_server_start(void)
{
    if (frame_queue == NULL || bridge_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(bridge_task_main, "c6_proxy", 6144, NULL, 5,
                    &bridge_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Logical frame and PubSub dispatcher started");
    return ESP_OK;
}

esp_err_t robusto_c6_proxy_server_deinit(void)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t error;

    error = robusto_message_frontend_register(ROBUSTO_PROXY_SDIO_REQUEST_MSG_ID,
                                              NULL);
    if (error != ESP_OK && first_error == ESP_OK) {
        first_error = error;
    }
    if (bridge_task != NULL) {
        vTaskDelete(bridge_task);
        bridge_task = NULL;
    }
    if (frame_queue != NULL) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
    if (!robusto_proxy_pubsub_server_adapter_deinit(&pubsub_adapter)) {
        return first_error == ESP_OK ? ESP_FAIL : first_error;
    }
    robusto_c6_pubsub_backend_deinit(&pubsub_backend);
    memset(&pubsub_adapter, 0, sizeof(pubsub_adapter));
    memset(&proxy_service, 0, sizeof(proxy_service));
    return first_error;
}