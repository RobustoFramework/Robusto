#include "robusto_proxy_sdio_c6.h"

#include "nvs_flash.h"
#include "robusto_c6_control_frontend.h"
#include "robusto_c6_pubsub_backend.h"
#include "robusto_c6_proxy_binding.h"
#include "robusto_c6_recovery.h"
#include "robusto_init.h"
#include "robusto_proxy_sdio_device.h"

#include "esp_log.h"

static const char *TAG = "C6Proxy";

void robusto_proxy_sdio_c6_set_pubsub_topic_hooks(
    robusto_proxy_sdio_c6_topic_hook_t subscribe_hook,
    robusto_proxy_sdio_c6_topic_hook_t unsubscribe_hook,
    void *hook_context)
{
    robusto_c6_pubsub_backend_set_topic_hooks(subscribe_hook,
                                              unsubscribe_hook,
                                              hook_context);
}

esp_err_t robusto_proxy_sdio_c6_start(void)
{
    ESP_LOGI(TAG, "C6 stage: nvs_flash_init()");
    esp_err_t error = nvs_flash_init();
    rob_ret_val_t result;

    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_c6_recovery_boot_guard()");
    error = robusto_c6_recovery_boot_guard();
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_proxy_sdio_device_init()");
    error = robusto_proxy_sdio_device_init();
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_c6_control_frontend_init()");
    error = robusto_c6_control_frontend_init();
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_c6_recovery_init()");
    error = robusto_c6_recovery_init();
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_c6_proxy_service_register()");
    result = robusto_c6_proxy_service_register();
    if (result == ROB_OK) {
        ESP_LOGI(TAG, "C6 stage: init_robusto_checked()");
        result = init_robusto_checked();
    }
    if (result != ROB_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "C6 stage: robusto_proxy_sdio_device_start()");
    return robusto_proxy_sdio_device_start();
}
