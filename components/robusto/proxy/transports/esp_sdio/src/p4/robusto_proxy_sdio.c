#include "robusto_proxy_sdio.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "robusto_proxy_sdio_messages.h"
#include "robusto_proxy_sdio_host.h"
#include "robusto_proxy_frame.h"
#include "robusto_proxy_lifecycle.h"
#include "robusto_proxy_pubsub_client.h"

static const char *TAG = "robusto_p4_proxy";

#define P4_RECEIVE_TASK_STACK_SIZE 4096U
#define P4_EVENT_TASK_STACK_SIZE 4096U
#define P4_RECEIVE_TIMEOUT_MS 100U
#define P4_TASK_STOP_TIMEOUT_MS 1000U
#define P4_RECEIVE_TASK_STOPPED BIT0
#define P4_EVENT_TASK_STOPPED BIT1
#define P4_PROXY_REQUEST_TIMEOUT_MS 2000U
#define P4_PROXY_RUNLEVEL 1U
#define ROBUSTO_PROXY_SDIO_EVENT_QUEUE_CAPACITY 4U

typedef struct robusto_proxy_sdio_frame_item {
    size_t size;
    uint8_t bytes[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
} robusto_proxy_sdio_frame_item_t;

typedef struct robusto_proxy_sdio {
    robusto_proxy_client_t client;
    robusto_proxy_client_service_t service;
    robusto_proxy_client_service_config_t service_config;
    uint8_t request_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    uint8_t response_frame[ROBUSTO_PROXY_SLOT_SIZE_BYTES];
    robusto_proxy_sdio_frame_item_t exchange_item;
    robusto_proxy_sdio_frame_item_t receive_item;
    robusto_proxy_sdio_frame_item_t event_item;
    QueueHandle_t response_queue;
    StaticQueue_t response_queue_control;
    uint8_t response_queue_storage[sizeof(robusto_proxy_sdio_frame_item_t)];
    QueueHandle_t event_queue;
    StaticQueue_t event_queue_control;
    uint8_t event_queue_storage[ROBUSTO_PROXY_SDIO_EVENT_QUEUE_CAPACITY *
                                sizeof(robusto_proxy_sdio_frame_item_t)];
    EventGroupHandle_t worker_events;
    StaticEventGroup_t worker_events_storage;
    SemaphoreHandle_t exchange_mutex;
    StaticSemaphore_t exchange_mutex_storage;
    TaskHandle_t receive_task;
    TaskHandle_t event_task;
    portMUX_TYPE response_lock;
    uint32_t expected_correlation_id;
    bool awaiting_response;
    bool transport_started;
    bool stopping;
    uint32_t dropped_events;
    robusto_proxy_pubsub_client_subscription_t *subscriptions;
    uint16_t subscription_capacity;
} robusto_proxy_sdio_t;

static robusto_proxy_sdio_t proxy;

static uint64_t random_nonzero_u64(void)
{
    uint64_t value;

    do {
        value = ((uint64_t)esp_random() << 32) | esp_random();
    } while (value == 0U);
    return value;
}

static uint32_t random_nonzero_u32(void)
{
    uint32_t value;

    do {
        value = esp_random();
    } while (value == 0U);
    return value;
}

static rob_ret_val_t map_exchange_error(esp_err_t error)
{
    switch (error) {
        case ESP_OK:
            return ROB_OK;
        case ESP_ERR_TIMEOUT:
            return ROB_ERR_TIMEOUT;
        case ESP_ERR_NO_MEM:
            return ROB_ERR_OUT_OF_MEMORY;
        case ESP_ERR_INVALID_ARG:
            return ROB_ERR_INVALID_ARG;
        case ESP_ERR_INVALID_STATE:
            return ROB_ERR_NOT_READY;
        case ESP_ERR_INVALID_SIZE:
            return ROB_ERR_MESSAGE_TOO_LONG;
        case ESP_ERR_INVALID_RESPONSE:
            return ROB_ERR_PARSING_FAILED;
        default:
            return ROB_ERR_SEND_FAIL;
    }
}

static rob_ret_val_t p4_exchange(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size,
    uint32_t timeout_ms,
    robusto_proxy_transfer_acceptance_t *acceptance)
{
    robusto_proxy_sdio_t *binding = context;
    const robusto_proxy_frame_header_t *request_header;
    robusto_proxy_sdio_frame_item_t *item;
    uint32_t message_id = 0U;
    uint32_t remaining_ms;
    int64_t deadline_us;
    esp_err_t error;

    if (binding == NULL || request == NULL ||
        request_size < ROBUSTO_PROXY_HEADER_SIZE_BYTES ||
        response == NULL || response_size == NULL || acceptance == NULL) {
        return ROB_ERR_INVALID_ARG;
    }
    deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);
    if (xSemaphoreTake(binding->exchange_mutex,
                       pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ROB_ERR_TIMEOUT;
    }
    *response_size = 0U;
    *acceptance = ROBUSTO_PROXY_TRANSFER_NOT_ACCEPTED;
    item = &binding->exchange_item;
    int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        xSemaphoreGive(binding->exchange_mutex);
        return ROB_ERR_TIMEOUT;
    }
    remaining_ms = (uint32_t)((remaining_us + 999) / 1000);
    request_header = (const robusto_proxy_frame_header_t *)request;
    if (binding->transport_started) {
        xQueueReset(binding->response_queue);
        portENTER_CRITICAL(&binding->response_lock);
        binding->expected_correlation_id = request_header->correlation_id;
        binding->awaiting_response = true;
        portEXIT_CRITICAL(&binding->response_lock);
    }
    ESP_LOGI(TAG,
             "request domain=%u opcode=%u correlation=0x%08lx size=%u",
             request_header->domain, request_header->opcode,
             (unsigned long)request_header->correlation_id,
             (unsigned int)request_size);

    error = robusto_proxy_sdio_host_send(ROBUSTO_PROXY_SDIO_REQUEST_MSG_ID,
                                      request, request_size, remaining_ms);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&binding->response_lock);
        binding->awaiting_response = false;
        portEXIT_CRITICAL(&binding->response_lock);
        ESP_LOGE(TAG, "send opcode=%u: %s", request_header->opcode,
                 esp_err_to_name(error));
        rob_ret_val_t result = map_exchange_error(error);
        xSemaphoreGive(binding->exchange_mutex);
        return result;
    }
    *acceptance = ROBUSTO_PROXY_TRANSFER_ACCEPTED;
    if (binding->transport_started) {
        for (;;) {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            TickType_t remaining_ticks = remaining_us > 0
                                             ? pdMS_TO_TICKS(
                                                   (remaining_us + 999) / 1000)
                                             : 0;
            const robusto_proxy_frame_header_t *response_header;

            if (xQueueReceive(binding->response_queue, item,
                              remaining_ticks) != pdTRUE) {
                portENTER_CRITICAL(&binding->response_lock);
                binding->awaiting_response = false;
                portEXIT_CRITICAL(&binding->response_lock);
                xSemaphoreGive(binding->exchange_mutex);
                return ROB_ERR_TIMEOUT;
            }
            if (item->size < ROBUSTO_PROXY_HEADER_SIZE_BYTES) {
                ESP_LOGW(TAG, "drop short response size=%u",
                         (unsigned int)item->size);
                continue;
            }
            response_header = (const robusto_proxy_frame_header_t *)item->bytes;
            if (response_header->correlation_id ==
                request_header->correlation_id) {
                break;
            }
            ESP_LOGW(TAG, "drop queued stale response correlation=0x%08lx",
                     (unsigned long)response_header->correlation_id);
        }
        portENTER_CRITICAL(&binding->response_lock);
        binding->awaiting_response = false;
        portEXIT_CRITICAL(&binding->response_lock);
        if (item->size > response_capacity) {
            xSemaphoreGive(binding->exchange_mutex);
            return ROB_ERR_MESSAGE_TOO_LONG;
        }
        memcpy(response, item->bytes, item->size);
        *response_size = item->size;
        error = ESP_OK;
        message_id = ROBUSTO_PROXY_SDIO_RESPONSE_MSG_ID;
    } else {
        for (;;) {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            uint32_t remaining_ms = remaining_us > 0
                                        ? (uint32_t)((remaining_us + 999) / 1000)
                                        : 0U;

            error = robusto_proxy_sdio_host_receive(&message_id, response,
                                                 response_capacity,
                                                 response_size, remaining_ms);
            if (error != ESP_OK || message_id != ROBUSTO_PROXY_SDIO_EVENT_MSG_ID) {
                break;
            }
            rob_ret_val_t event_result = robusto_proxy_pubsub_handle_event(
                &binding->client, response, *response_size);
            if (event_result != ROB_OK) {
                ESP_LOGE(TAG, "dispatch delivery event result=%d", event_result);
            }
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "receive opcode=%u: %s", request_header->opcode,
                 esp_err_to_name(error));
        rob_ret_val_t result = map_exchange_error(error);
        xSemaphoreGive(binding->exchange_mutex);
        return result;
    }
    ESP_LOGI(TAG, "response opcode=%u message=0x%08lx size=%u",
             request_header->opcode, (unsigned long)message_id,
             (unsigned int)*response_size);
    rob_ret_val_t result = message_id == ROBUSTO_PROXY_SDIO_RESPONSE_MSG_ID
                               ? ROB_OK
                               : ROB_ERR_PARSING_FAILED;
    xSemaphoreGive(binding->exchange_mutex);
    return result;
}

static uint32_t p4_now_ms(void *context)
{
    (void)context;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void p4_wait_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static uint32_t p4_jitter_ms(void *context, uint32_t maximum_ms)
{
    (void)context;
    if (maximum_ms == 0U) {
        return 0U;
    }
    if (maximum_ms == UINT32_MAX) {
        return esp_random();
    }
    return esp_random() % (maximum_ms + 1U);
}

static rob_ret_val_t p4_transport_init(void *context, char *log_prefix)
{
    robusto_proxy_sdio_t *binding = context;
    esp_err_t error;

    (void)log_prefix;
    if (binding == NULL) {
        return ROB_ERR_INVALID_ARG;
    }
    error = robusto_proxy_sdio_host_init();
    if (error == ESP_OK) {
        return ROB_OK;
    }
    return error == ESP_ERR_NO_MEM ? ROB_ERR_OUT_OF_MEMORY : ROB_ERR_INIT_FAIL;
}

static void receive_task_main(void *context)
{
    robusto_proxy_sdio_t *binding = context;
    robusto_proxy_sdio_frame_item_t *item = &binding->receive_item;
    uint32_t message_id;

    while (!binding->stopping) {
        item->size = 0U;
        esp_err_t error = robusto_proxy_sdio_host_receive(
            &message_id, item->bytes, sizeof(item->bytes), &item->size,
            P4_RECEIVE_TIMEOUT_MS);
        if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NOT_FOUND) {
            continue;
        }
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "receive worker: %s", esp_err_to_name(error));
            continue;
        }
        if (message_id == ROBUSTO_PROXY_SDIO_RESPONSE_MSG_ID) {
            const robusto_proxy_frame_header_t *header =
                (const robusto_proxy_frame_header_t *)item->bytes;
            bool expected;

            portENTER_CRITICAL(&binding->response_lock);
            expected = binding->awaiting_response &&
                       item->size >= ROBUSTO_PROXY_HEADER_SIZE_BYTES &&
                       header->correlation_id == binding->expected_correlation_id;
            portEXIT_CRITICAL(&binding->response_lock);
            if (!expected) {
                ESP_LOGW(TAG, "drop stale response correlation=0x%08lx",
                         item->size >= ROBUSTO_PROXY_HEADER_SIZE_BYTES
                             ? (unsigned long)header->correlation_id
                             : 0UL);
            } else if (xQueueSend(binding->response_queue, item, 0) != pdTRUE) {
                ESP_LOGE(TAG, "response queue full");
            }
        } else if (message_id == ROBUSTO_PROXY_SDIO_EVENT_MSG_ID) {
            if (xQueueSend(binding->event_queue, item, 0) != pdTRUE) {
                binding->dropped_events += 1U;
                ESP_LOGE(TAG, "event queue full");
            }
        } else {
            ESP_LOGE(TAG, "unexpected message=0x%08lx",
                     (unsigned long)message_id);
        }
    }
    binding->receive_task = NULL;
    xEventGroupSetBits(binding->worker_events, P4_RECEIVE_TASK_STOPPED);
    vTaskDelete(NULL);
}

static void event_task_main(void *context)
{
    robusto_proxy_sdio_t *binding = context;
    robusto_proxy_sdio_frame_item_t *item = &binding->event_item;

    while (!binding->stopping) {
        if (xQueueReceive(binding->event_queue, item,
                          pdMS_TO_TICKS(P4_RECEIVE_TIMEOUT_MS)) != pdTRUE) {
            continue;
        }
        rob_ret_val_t result = robusto_proxy_pubsub_handle_event(
            &binding->client, item->bytes, item->size);
        if (result != ROB_OK) {
            ESP_LOGE(TAG, "dispatch delivery event result=%d", result);
        }
    }
    binding->event_task = NULL;
    xEventGroupSetBits(binding->worker_events, P4_EVENT_TASK_STOPPED);
    vTaskDelete(NULL);
}

static rob_ret_val_t p4_transport_start(void *context)
{
    robusto_proxy_sdio_t *binding = context;
    rob_ret_val_t result;

    if (binding == NULL || binding->transport_started) {
        return ROB_ERR_INVALID_ARG;
    }
    result = robusto_proxy_pubsub_configure(
        &binding->client, binding->subscriptions,
        binding->subscription_capacity);
    if (result != ROB_OK) {
        return result;
    }
    binding->response_queue = xQueueCreateStatic(
        1U, sizeof(robusto_proxy_sdio_frame_item_t),
        binding->response_queue_storage, &binding->response_queue_control);
    binding->event_queue = xQueueCreateStatic(
        ROBUSTO_PROXY_SDIO_EVENT_QUEUE_CAPACITY,
        sizeof(robusto_proxy_sdio_frame_item_t), binding->event_queue_storage,
        &binding->event_queue_control);
    binding->worker_events = xEventGroupCreateStatic(
        &binding->worker_events_storage);
    if (binding->response_queue == NULL || binding->event_queue == NULL ||
        binding->worker_events == NULL) {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    binding->stopping = false;
    xEventGroupClearBits(binding->worker_events,
                         P4_RECEIVE_TASK_STOPPED | P4_EVENT_TASK_STOPPED);
    if (xTaskCreate(receive_task_main, "proxy_rx", P4_RECEIVE_TASK_STACK_SIZE,
                    binding, 6, &binding->receive_task) != pdPASS) {
        binding->receive_task = NULL;
        return ROB_ERR_OUT_OF_MEMORY;
    }
    if (xTaskCreate(event_task_main, "proxy_events", P4_EVENT_TASK_STACK_SIZE,
                    binding, 5, &binding->event_task) != pdPASS) {
        binding->stopping = true;
        (void)xEventGroupWaitBits(binding->worker_events,
                                  P4_RECEIVE_TASK_STOPPED, pdFALSE, pdTRUE,
                                  pdMS_TO_TICKS(P4_TASK_STOP_TIMEOUT_MS));
        return ROB_ERR_OUT_OF_MEMORY;
    }
    binding->transport_started = true;
    return ROB_OK;
}

static rob_ret_val_t p4_transport_stop(void *context)
{
    robusto_proxy_sdio_t *binding = context;
    EventBits_t stopped;

    if (binding == NULL) {
        return ROB_ERR_INVALID_ARG;
    }
    if (binding->transport_started) {
        binding->stopping = true;
        stopped = xEventGroupWaitBits(
            binding->worker_events,
            P4_RECEIVE_TASK_STOPPED | P4_EVENT_TASK_STOPPED,
            pdFALSE, pdTRUE, pdMS_TO_TICKS(P4_TASK_STOP_TIMEOUT_MS));
        if ((stopped & (P4_RECEIVE_TASK_STOPPED | P4_EVENT_TASK_STOPPED)) !=
            (P4_RECEIVE_TASK_STOPPED | P4_EVENT_TASK_STOPPED)) {
            return ROB_ERR_TIMEOUT;
        }
        binding->transport_started = false;
    }
    return map_exchange_error(robusto_proxy_sdio_host_deinit());
}

rob_ret_val_t robusto_proxy_sdio_register(
    robusto_proxy_pubsub_client_subscription_t *subscriptions,
    uint16_t subscription_capacity)
{
    robusto_proxy_sdio_t *binding = &proxy;

    if (subscriptions == NULL || subscription_capacity == 0U ||
        binding->service.registered) {
        return ROB_ERR_INVALID_ARG;
    }

    memset(binding, 0, sizeof(*binding));
    binding->response_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    binding->exchange_mutex = xSemaphoreCreateMutexStatic(
        &binding->exchange_mutex_storage);
    if (binding->exchange_mutex == NULL) {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    binding->service_config.client = &binding->client;
    binding->service_config.client_config.profile =
        ROBUSTO_PROXY_PROFILE_LOW_MEMORY;
    binding->service_config.client_config.controller_boot_id =
        random_nonzero_u64();
    binding->service_config.client_config.correlation_seed =
        random_nonzero_u32();
    binding->service_config.client_config.sequence_seed = random_nonzero_u32();
    binding->service_config.client_config.operation_seed = random_nonzero_u64();
    binding->service_config.client_config.request_timeout_ms =
        P4_PROXY_REQUEST_TIMEOUT_MS;
    binding->service_config.client_config.exchange = p4_exchange;
    binding->service_config.client_config.transport_context = binding;
    binding->service_config.client_config.now_ms = p4_now_ms;
    binding->service_config.client_config.wait_ms = p4_wait_ms;
    binding->service_config.client_config.retry_jitter_ms = p4_jitter_ms;
    binding->service_config.client_config.request_frame = binding->request_frame;
    binding->service_config.client_config.request_frame_size =
        sizeof(binding->request_frame);
    binding->service_config.client_config.response_frame = binding->response_frame;
    binding->service_config.client_config.response_frame_size =
        sizeof(binding->response_frame);
    binding->service_config.transport_init = p4_transport_init;
    binding->service_config.transport_start = p4_transport_start;
    binding->service_config.transport_stop = p4_transport_stop;
    binding->service_config.transport_lifecycle_context = binding;
    binding->service_config.runlevel = P4_PROXY_RUNLEVEL;
    binding->subscriptions = subscriptions;
    binding->subscription_capacity = subscription_capacity;
    return robusto_proxy_client_service_register(
        &binding->service, &binding->service_config);
}

robusto_proxy_client_t *robusto_proxy_sdio(void)
{
    return &proxy.client;
}

bool robusto_proxy_sdio_is_registered(void)
{
    return proxy.service.registered;
}
