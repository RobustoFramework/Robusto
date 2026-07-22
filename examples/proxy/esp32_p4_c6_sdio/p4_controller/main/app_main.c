#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "robusto_init.h"
#include "robusto_proxy_pubsub_client.h"
#include "robusto_proxy_sdio.h"

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
