#include "c6_firmware_manifest.h"
#include "c6_factory_bootstrap.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "robusto_proxy_sdio_c6_provisioning.h"

#define C6_FIRMWARE_PARTITION "slave_fw"
#define MIGRATION_NAMESPACE "c6_migrate"
#define MIGRATION_PHASE_KEY "phase"

typedef enum migration_phase {
    MIGRATION_PHASE_DISCOVER = 0,
    MIGRATION_PHASE_BOOTSTRAP_PENDING = 1,
    MIGRATION_PHASE_BOOTSTRAP_READY = 2,
    MIGRATION_PHASE_FINAL_PENDING = 3,
    MIGRATION_PHASE_FINAL_VERIFY = 4,
    MIGRATION_PHASE_COMPLETE = 5,
} migration_phase_t;

static const char *TAG = "c6_provisioning";

static void halt(void)
{
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void restart_for_phase(const char *message)
{
    ESP_LOGI(TAG, "%s", message);
    vTaskDelay(pdMS_TO_TICKS(3000U));
    esp_restart();
}

static esp_err_t load_phase(migration_phase_t *phase)
{
    nvs_handle_t handle;
    uint8_t value = MIGRATION_PHASE_DISCOVER;
    esp_err_t error;

    if (phase == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    error = nvs_open(MIGRATION_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *phase = MIGRATION_PHASE_DISCOVER;
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_get_u8(handle, MIGRATION_PHASE_KEY, &value);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *phase = MIGRATION_PHASE_DISCOVER;
        return ESP_OK;
    }
    if (error != ESP_OK || value < MIGRATION_PHASE_BOOTSTRAP_PENDING ||
        value > MIGRATION_PHASE_COMPLETE) {
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }
    *phase = (migration_phase_t)value;
    return ESP_OK;
}

static esp_err_t store_phase(migration_phase_t phase)
{
    nvs_handle_t handle;
    esp_err_t error;

    if (phase < MIGRATION_PHASE_BOOTSTRAP_PENDING ||
        phase > MIGRATION_PHASE_COMPLETE) {
        return ESP_ERR_INVALID_ARG;
    }
    error = nvs_open(MIGRATION_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_u8(handle, MIGRATION_PHASE_KEY, (uint8_t)phase);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static esp_err_t provision_raw(
    const robusto_proxy_sdio_c6_provisioning_config_t *config,
    bool *restart_required,
    bool *identity_received)
{
    return robusto_proxy_sdio_c6_provision(
        config, restart_required, identity_received);
}

void app_main(void)
{
    const robusto_proxy_sdio_c6_provisioning_config_t bootstrap = {
        .partition_label = C6_FIRMWARE_PARTITION,
        .image_offset = C6_BOOTSTRAP_OFFSET,
        .image_size = C6_BOOTSTRAP_SIZE,
        .image_sha256 = C6_BOOTSTRAP_SHA256,
        .elf_sha256 = C6_BOOTSTRAP_ELF_SHA256,
    };
    const robusto_proxy_sdio_c6_provisioning_config_t final = {
        .partition_label = C6_FIRMWARE_PARTITION,
        .image_offset = C6_FINAL_OFFSET,
        .image_size = C6_FINAL_SIZE,
        .image_sha256 = C6_FINAL_SHA256,
        .elf_sha256 = C6_FINAL_ELF_SHA256,
    };
    const c6_factory_bootstrap_config_t factory_bootstrap = {
        .partition_label = C6_FIRMWARE_PARTITION,
        .image_offset = C6_BOOTSTRAP_OFFSET,
        .image_size = C6_BOOTSTRAP_SIZE,
        .image_sha256 = C6_BOOTSTRAP_SHA256,
    };
    migration_phase_t phase;
    bool restart_required = false;
    bool identity_received = false;
    esp_err_t error = nvs_flash_init();

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Initialize migration state: %s",
                 esp_err_to_name(error));
        halt();
    }
    error = load_phase(&phase);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Load migration phase: %s", esp_err_to_name(error));
        halt();
    }

    if (phase == MIGRATION_PHASE_DISCOVER) {
        error = provision_raw(&final, &restart_required, &identity_received);
        if (error == ESP_OK) {
            phase = restart_required ? MIGRATION_PHASE_FINAL_PENDING
                                     : MIGRATION_PHASE_FINAL_VERIFY;
            error = store_phase(phase);
            if (error == ESP_OK) {
                restart_for_phase("Final raw C6 found or installed; restarting for verification");
            }
        } else if (!identity_received) {
            error = store_phase(MIGRATION_PHASE_BOOTSTRAP_PENDING);
            if (error == ESP_OK) {
                error = c6_factory_bootstrap_install(&factory_bootstrap);
            }
            if (error == ESP_OK) {
                restart_for_phase("Factory bootstrap transferred; restarting for raw identity");
            }
        }
    } else if (phase == MIGRATION_PHASE_BOOTSTRAP_PENDING) {
        error = provision_raw(&bootstrap, &restart_required,
                              &identity_received);
        if (error != ESP_OK && !identity_received) {
            error = c6_factory_bootstrap_install(&factory_bootstrap);
            if (error == ESP_OK) {
                restart_for_phase("Factory bootstrap retransferred; restarting for raw identity");
            }
        } else if (error == ESP_OK && restart_required) {
            restart_for_phase("Bootstrap activated; restarting for exact confirmation");
        } else if (error == ESP_OK) {
            error = store_phase(MIGRATION_PHASE_BOOTSTRAP_READY);
            if (error == ESP_OK) {
                restart_for_phase("Bootstrap confirmed; restarting for final installation");
            }
        }
    } else if (phase == MIGRATION_PHASE_BOOTSTRAP_READY) {
        error = store_phase(MIGRATION_PHASE_FINAL_PENDING);
        if (error == ESP_OK) {
            error = provision_raw(&final, &restart_required,
                                  &identity_received);
        }
        if (error == ESP_OK && restart_required) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase("Final C6 activated; restarting for confirmation");
            }
        } else if (error == ESP_OK) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase("Final C6 confirmed; restarting for durability check");
            }
        }
    } else if (phase == MIGRATION_PHASE_FINAL_PENDING) {
        error = provision_raw(&final, &restart_required, &identity_received);
        if (error == ESP_OK && restart_required) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase("Final C6 transfer resumed; restarting for confirmation");
            }
        } else if (error == ESP_OK) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase("Final C6 confirmed; restarting for durability check");
            }
        }
    } else if (phase == MIGRATION_PHASE_FINAL_VERIFY) {
        error = robusto_proxy_sdio_c6_confirm_after_activation(
            &final, &identity_received);
        if (error == ESP_OK && identity_received) {
            error = store_phase(MIGRATION_PHASE_COMPLETE);
        }
        if (error == ESP_OK) {
            ESP_LOGI(TAG, "Final C6 exact identity is confirmed");
            halt();
        } else if (identity_received) {
            error = store_phase(MIGRATION_PHASE_FINAL_PENDING);
            if (error == ESP_OK) {
                restart_for_phase(
                    "Final C6 rolled back before confirmation; restarting installation");
            }
        }
    } else {
        error = provision_raw(&final, &restart_required, &identity_received);
        if (error == ESP_OK && (restart_required || !identity_received)) {
            error = ESP_ERR_INVALID_STATE;
        }
        if (error == ESP_OK) {
            ESP_LOGI(TAG,
                     "Final C6 exact identity is confirmed and survives P4 restart");
            halt();
        }
    }

    ESP_LOGE(TAG, "C6 migration phase %u failed: %s",
             (unsigned int)phase, esp_err_to_name(error));
    halt();
}