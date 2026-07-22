#include "c6_firmware_manifest.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "robusto_proxy_sdio_c6_provisioning.h"

#define C6_FIRMWARE_PARTITION "slave_fw"

static const char *TAG = "c6_provisioning";

static void halt(void)
{
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void app_main(void)
{
    const robusto_proxy_sdio_c6_provisioning_config_t config = {
        .partition_label = C6_FIRMWARE_PARTITION,
        .image_offset = C6_FIRMWARE_OFFSET,
        .image_size = C6_FIRMWARE_SIZE,
        .image_sha256 = C6_FIRMWARE_SHA256,
        .elf_sha256 = C6_ELF_SHA256,
    };
    bool restart_required;
    esp_err_t error = robusto_proxy_sdio_c6_provision(
        &config, &restart_required);

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "C6 installation failed: %s", esp_err_to_name(error));
        halt();
    }
    if (!restart_required) {
        ESP_LOGI(TAG, "C6 image is installed and hash-bound confirmation passed");
        halt();
    }
    ESP_LOGI(TAG, "C6 image activated; restarting P4 for exact confirmation");
    vTaskDelay(pdMS_TO_TICKS(3000U));
    esp_restart();
}