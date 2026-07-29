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

static const char *migration_phase_name(migration_phase_t phase)
{
    switch (phase) {
    case MIGRATION_PHASE_DISCOVER:
        return "discover";
    case MIGRATION_PHASE_BOOTSTRAP_PENDING:
        return "bootstrap_pending";
    case MIGRATION_PHASE_BOOTSTRAP_READY:
        return "bootstrap_ready";
    case MIGRATION_PHASE_FINAL_PENDING:
        return "final_pending";
    case MIGRATION_PHASE_FINAL_VERIFY:
        return "final_verify";
    case MIGRATION_PHASE_COMPLETE:
        return "complete";
    default:
        return "unknown";
    }
}

static const char *migration_error_detail(migration_phase_t phase,
                                          esp_err_t error,
                                          bool restart_required,
                                          bool identity_received)
{
    if (error == ESP_ERR_INVALID_STATE) {
        if (phase == MIGRATION_PHASE_COMPLETE && !identity_received) {
            return "stored phase says complete, but the delegate did not return a raw-SDIO identity";
        }
        if (phase == MIGRATION_PHASE_FINAL_VERIFY && identity_received) {
            return "delegate answered, but its exact ELF identity was not confirmed after activation";
        }
        if (phase == MIGRATION_PHASE_COMPLETE && restart_required) {
            return "stored phase says complete, but a newer final delegate image was installed and restart is still required";
        }
    }
    return "see preceding provisioning logs for the failing operation";
}

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

static esp_err_t clear_phase(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(MIGRATION_NAMESPACE, NVS_READWRITE, &handle);

    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_erase_key(handle, MIGRATION_PHASE_KEY);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_OK;
    }
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

    /* Any phase resumed after activating a C6 image must confirm the exact
     * running ELF first. Reinstall is only valid if confirmation proves the
     * active image is not the expected artifact. */

    if (phase == MIGRATION_PHASE_DISCOVER) {
        error = provision_raw(&final, &restart_required, &identity_received);
        if (error == ESP_OK) {
            phase = restart_required ? MIGRATION_PHASE_FINAL_PENDING
                                     : MIGRATION_PHASE_FINAL_VERIFY;
            error = store_phase(phase);
            if (error == ESP_OK) {
                restart_for_phase(restart_required
                                      ? "Final raw C6 installed; restarting for confirmation"
                                      : "Final raw C6 already confirmed; restarting for durability check");
            }
        } else if (!identity_received) {
            error = c6_factory_bootstrap_install(&factory_bootstrap);
            if (error == ESP_OK) {
                error = store_phase(MIGRATION_PHASE_BOOTSTRAP_PENDING);
            }
            if (error == ESP_OK) {
                restart_for_phase("Factory bootstrap transferred; restarting for exact confirmation");
            }
        }
    } else if (phase == MIGRATION_PHASE_BOOTSTRAP_PENDING) {
        error = robusto_proxy_sdio_c6_confirm_after_activation(
            &bootstrap, &identity_received);
        if (error == ESP_OK && identity_received) {
            error = store_phase(MIGRATION_PHASE_BOOTSTRAP_READY);
            if (error == ESP_OK) {
                restart_for_phase("Bootstrap confirmed after activation; restarting for final installation");
            }
        } else {
            if (!identity_received) {
                esp_err_t phase_error = clear_phase();
                if (phase_error != ESP_OK) {
                    ESP_LOGE(TAG, "Clear stale bootstrap phase: %s",
                             esp_err_to_name(phase_error));
                    error = phase_error;
                }
            }
            if (error != ESP_OK && identity_received) {
                halt();
            }
            error = provision_raw(&bootstrap, &restart_required,
                                  &identity_received);
            if (error != ESP_OK && !identity_received) {
                error = c6_factory_bootstrap_install(&factory_bootstrap);
                if (error == ESP_OK) {
                    restart_for_phase("Factory bootstrap retransferred; restarting for exact confirmation");
                }
            } else if (error == ESP_OK && restart_required) {
                restart_for_phase("Bootstrap image updated; restarting for exact confirmation");
            } else if (error == ESP_OK) {
                error = store_phase(MIGRATION_PHASE_BOOTSTRAP_READY);
                if (error == ESP_OK) {
                    restart_for_phase("Bootstrap confirmed; restarting for final installation");
                }
            }
        }
    } else if (phase == MIGRATION_PHASE_BOOTSTRAP_READY) {
        error = provision_raw(&final, &restart_required,
                              &identity_received);
        if (error == ESP_OK && restart_required) {
            error = store_phase(MIGRATION_PHASE_FINAL_PENDING);
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
        error = robusto_proxy_sdio_c6_confirm_after_activation(
            &final, &identity_received);
        if (error == ESP_OK && identity_received) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase("Final C6 confirmed after activation; restarting for durability check");
            }
        } else {
            error = provision_raw(&final, &restart_required,
                                  &identity_received);
            if (error == ESP_OK && restart_required) {
                error = store_phase(MIGRATION_PHASE_FINAL_PENDING);
                if (error == ESP_OK) {
                    restart_for_phase("Final C6 reinstalled; restarting for confirmation");
                }
            } else if (error == ESP_OK) {
                error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
                if (error == ESP_OK) {
                    restart_for_phase("Final C6 confirmed; restarting for durability check");
                }
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
        if (error == ESP_OK && restart_required) {
            error = store_phase(MIGRATION_PHASE_FINAL_VERIFY);
            if (error == ESP_OK) {
                restart_for_phase(
                    "Final C6 image updated; restarting for confirmation");
            }
        } else if (error == ESP_OK && !identity_received) {
            error = ESP_ERR_INVALID_STATE;
        }
        if (error == ESP_OK) {
            ESP_LOGI(TAG,
                     "Final C6 exact identity is confirmed and survives P4 restart");
            halt();
        }
    }

    ESP_LOGE(TAG,
             "C6 migration phase %u (%s) failed: %s; identity_received=%s restart_required=%s; %s",
             (unsigned int)phase, migration_phase_name(phase),
             esp_err_to_name(error), identity_received ? "true" : "false",
             restart_required ? "true" : "false",
             migration_error_detail(phase, error, restart_required,
                                    identity_received));
    halt();
}