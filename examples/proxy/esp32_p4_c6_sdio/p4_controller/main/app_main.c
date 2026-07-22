#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "robusto_init.h"
#include "robusto_proxy_pubsub_client.h"
#include "robusto_proxy_sdio.h"

#define LARGE_PUBLISH_BYTES (200U * 1024U)

static const char *TAG = "p4_proxy_main";
static robusto_proxy_pubsub_client_subscription_t subscriptions[4];
static StaticSemaphore_t delivery_semaphore_storage;
static SemaphoreHandle_t delivery_semaphore;
static rob_ret_val_t delivery_result;

static rob_ret_val_t delivery_callback(void *context, uint8_t *data,
                                       uint32_t data_length)
{
    const uint8_t *expected = context;

    delivery_result = data_length == 4U && memcmp(data, expected, 4U) == 0
                          ? ROB_OK
                          : ROB_ERR_PARSING_FAILED;
    xSemaphoreGive(delivery_semaphore);
    return delivery_result;
}

static rob_ret_val_t run_pubsub_example(void)
{
    static uint8_t payload[] = {0x50U, 0x34U, 0x43U, 0x36U};
    robusto_proxy_pubsub_client_subscription_t *subscription = NULL;
    rob_ret_val_t result;

    delivery_semaphore = xSemaphoreCreateBinaryStatic(
        &delivery_semaphore_storage);
    if (delivery_semaphore == NULL) {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    delivery_result = ROB_ERR_TIMEOUT;

    result = robusto_proxy_pubsub_subscribe(
        robusto_proxy_sdio(), "proxy.test.loopback",
        delivery_callback,
        payload, &subscription);
    if (result != ROB_OK) {
        return result;
    }
    result = robusto_proxy_pubsub_publish(
        robusto_proxy_sdio(), "proxy.test.loopback", payload,
        sizeof(payload));
    if (result != ROB_OK) {
        return result;
    }
    if (xSemaphoreTake(delivery_semaphore, pdMS_TO_TICKS(2000U)) != pdTRUE) {
        return ROB_ERR_TIMEOUT;
    }
    return delivery_result;
}

static uint8_t expected_byte(uint32_t offset)
{
    return (uint8_t)((offset * 31U + (offset >> 8U) + 0x5AU) & 0xFFU);
}

static rob_ret_val_t run_large_publish_example(void)
{
    const uint32_t payload_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    size_t largest_block = heap_caps_get_largest_free_block(payload_caps);
    uint8_t *payload;
    rob_ret_val_t result;

    ESP_LOGI(TAG, "largest PSRAM block before large publish: %u bytes",
             (unsigned int)largest_block);
    payload = heap_caps_malloc(LARGE_PUBLISH_BYTES, payload_caps);
    if (payload == NULL) {
        return ROB_ERR_OUT_OF_MEMORY;
    }
    for (uint32_t offset = 0U; offset < LARGE_PUBLISH_BYTES; ++offset) {
        payload[offset] = expected_byte(offset);
    }
    result = robusto_proxy_pubsub_publish(
        robusto_proxy_sdio(), "proxy.test.large", payload,
        LARGE_PUBLISH_BYTES);
    heap_caps_free(payload);
    if (result == ROB_OK) {
        ESP_LOGI(TAG, "[PASS] sent %u-byte chunked publish",
                 LARGE_PUBLISH_BYTES);
    }
    return result;
}

static void halt(void)
{
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void app_main(void)
{
    rob_ret_val_t result;

    result = robusto_proxy_sdio_register(
        subscriptions,
        sizeof(subscriptions) / sizeof(subscriptions[0]));
    if (result == ROB_OK) {
        result = init_robusto_checked();
    }
    if (result == ROB_OK) {
        result = run_pubsub_example();
    }
    if (result == ROB_OK) {
        result = run_large_publish_example();
    }
    if (result != ROB_OK) {
        rob_ret_val_t stop_result = ROB_OK;

        ESP_LOGE(TAG, "proxy startup failed: %d", result);
        if (robusto_proxy_sdio_is_registered()) {
            stop_result = stop_robusto_checked();
        }
        if (stop_result != ROB_OK) {
            ESP_LOGE(TAG, "proxy shutdown failed: %d", stop_result);
        }
        halt();
    }
    ESP_LOGI(TAG, "remote PubSub example ready");
}
