#include "robusto_proxy_sdio_c6.h"

#include "nvs_flash.h"
#include "robusto_c6_control_frontend.h"
#include "robusto_c6_proxy_binding.h"
#include "robusto_c6_recovery.h"
#include "robusto_init.h"
#include "robusto_proxy_sdio_device.h"

esp_err_t robusto_proxy_sdio_c6_start(void)
{
    esp_err_t error = nvs_flash_init();
    rob_ret_val_t result;

    if (error != ESP_OK) {
        return error;
    }
    error = robusto_c6_recovery_boot_guard();
    if (error != ESP_OK) {
        return error;
    }
    error = robusto_proxy_sdio_device_init();
    if (error != ESP_OK) {
        return error;
    }
    error = robusto_c6_control_frontend_init();
    if (error != ESP_OK) {
        return error;
    }
    error = robusto_c6_recovery_init();
    if (error != ESP_OK) {
        return error;
    }
    result = robusto_c6_proxy_service_register();
    if (result == ROB_OK) {
        result = init_robusto_checked();
    }
    if (result != ROB_OK) {
        return ESP_FAIL;
    }
    return robusto_proxy_sdio_device_start();
}
