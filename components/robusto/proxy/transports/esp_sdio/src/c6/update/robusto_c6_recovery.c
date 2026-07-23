#include "robusto_c6_recovery.h"

#include <stdbool.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "robusto_c6_update_protocol.h"
#include "robusto_c6_recovery_protocol.h"
#include "robusto_message_frontend.h"

#define C6_CONFIRM_NAMESPACE "b0_confirm"
#define C6_CONFIRM_KEY "record"
#define C6_CONFIRM_MAGIC 0x42304346U
#define C6_CONFIRM_STATE_ARMED 1U
#define C6_CONFIRM_STATE_CONFIRMED 2U
#define C6_RECOVERY_REQUEST_QUEUE_CAPACITY 1U

typedef struct {
    uint32_t magic;
    uint8_t state;
    uint8_t reserved[3];
    uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE];
} c6_confirmation_record_t;

static const char *TAG = "robusto_c6_recovery";
static QueueHandle_t request_queue;
static TaskHandle_t recovery_task;

static esp_err_t load_confirmation(c6_confirmation_record_t *record,
                                   bool *present)
{
    if (record == NULL || present == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *present = false;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(C6_CONFIRM_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    size_t size = sizeof(*record);
    error = nvs_get_blob(handle, C6_CONFIRM_KEY, record, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (size != sizeof(*record) || record->magic != C6_CONFIRM_MAGIC ||
        (record->state != C6_CONFIRM_STATE_ARMED &&
         record->state != C6_CONFIRM_STATE_CONFIRMED)) {
        return ESP_ERR_INVALID_STATE;
    }
    *present = true;
    return ESP_OK;
}

static esp_err_t store_confirmation(
    uint8_t state,
    const uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE])
{
    c6_confirmation_record_t record = {
        .magic = C6_CONFIRM_MAGIC,
        .state = state,
    };
    memcpy(record.build_sha256, build_sha256, sizeof(record.build_sha256));
    nvs_handle_t handle;
    esp_err_t error = nvs_open(C6_CONFIRM_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, C6_CONFIRM_KEY, &record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static esp_err_t clear_confirmation(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(C6_CONFIRM_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_erase_key(handle, C6_CONFIRM_KEY);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

esp_err_t robusto_c6_recovery_boot_guard(void)
{
    const esp_app_desc_t *description = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    c6_confirmation_record_t record;
    bool present;

    if (description == NULL || running == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = load_confirmation(&record, &present);
    if (error != ESP_OK) {
        return error;
    }
    if (!present ||
        memcmp(record.build_sha256, description->app_elf_sha256,
               sizeof(record.build_sha256)) != 0) {
        return store_confirmation(C6_CONFIRM_STATE_ARMED,
                                  description->app_elf_sha256);
    }
    if (record.state != C6_CONFIRM_STATE_ARMED) {
        return ESP_OK;
    }
    const esp_partition_t *previous = esp_ota_get_next_update_partition(running);
    if (previous == NULL || previous == running) {
        return ESP_ERR_NOT_FOUND;
    }
    error = esp_ota_set_boot_partition(previous);
    if (error == ESP_ERR_OTA_VALIDATE_FAILED) {
        ESP_LOGW(TAG,
                 "Rollback target subtype 0x%02x is invalid; keeping current recovery image",
                 previous->subtype);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    error = clear_confirmation();
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGW(TAG, "Unconfirmed build reset; selecting subtype 0x%02x",
             previous->subtype);
    esp_restart();
    return ESP_FAIL;
}

static esp_err_t process_request(const robusto_c6_recovery_request_t *request)
{
    const esp_app_desc_t *description = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    c6_confirmation_record_t confirmation;
    bool confirmation_present;

    if (description == NULL || running == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = load_confirmation(&confirmation, &confirmation_present);
    if (error != ESP_OK) {
        return error;
    }
    bool confirmed = confirmation_present &&
                     confirmation.state == C6_CONFIRM_STATE_CONFIRMED &&
                     memcmp(confirmation.build_sha256,
                            description->app_elf_sha256,
                            sizeof(confirmation.build_sha256)) == 0;
    if (request->command == ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM) {
        if (!confirmation_present ||
            confirmation.state != C6_CONFIRM_STATE_ARMED ||
            memcmp(confirmation.build_sha256, description->app_elf_sha256,
                   sizeof(confirmation.build_sha256)) != 0 ||
            memcmp(request->build_sha256, description->app_elf_sha256,
                   sizeof(request->build_sha256)) != 0) {
            return ESP_ERR_INVALID_STATE;
        }
        error = store_confirmation(C6_CONFIRM_STATE_CONFIRMED,
                                   description->app_elf_sha256);
        if (error != ESP_OK) {
            return error;
        }
        confirmed = true;
    } else if (!confirmed) {
        error = store_confirmation(C6_CONFIRM_STATE_ARMED,
                                   description->app_elf_sha256);
        if (error != ESP_OK) {
            return error;
        }
    }
    robusto_c6_recovery_record_t record = {
        .magic = ROBUSTO_C6_RECOVERY_IDENTITY_MAGIC,
        .version = ROBUSTO_C6_RECOVERY_IDENTITY_VERSION,
        .generation = ROBUSTO_C6_UPDATE_GENERATION_CURRENT,
        .protocol_revision = ROBUSTO_C6_RECOVERY_PROTOCOL_REVISION,
        .updater_revision = ROBUSTO_C6_UPDATER_REVISION_CURRENT,
        .transport_frontend = ROBUSTO_C6_RECOVERY_FRONTEND_RAW_SDIO,
        .target_chip = ROBUSTO_C6_RECOVERY_TARGET_ESP32_C6,
        .partition_subtype = running->subtype,
        .boot_state = confirmed ? ROBUSTO_C6_RECOVERY_BOOT_CONFIRMED
                                : ROBUSTO_C6_RECOVERY_BOOT_PENDING_VERIFY,
    };
    memcpy(record.build_sha256, description->app_elf_sha256,
           sizeof(record.build_sha256));
    return robusto_message_frontend_send(ROBUSTO_C6_RECOVERY_IDENTITY_RECORD_MSG_ID,
                                         (const uint8_t *)&record,
                                         sizeof(record));
}

static void request_callback(uint32_t message_id,
                             const uint8_t *data,
                             size_t data_len)
{
    robusto_c6_recovery_request_t request;
    bool build_sha256_nonzero = false;

    if (message_id != ROBUSTO_C6_RECOVERY_IDENTITY_REQUEST_MSG_ID || data == NULL ||
        data_len != sizeof(request)) {
        ESP_LOGE(TAG, "Invalid identity request envelope");
        return;
    }
    memcpy(&request, data, sizeof(request));
    for (size_t index = 0; index < sizeof(request.build_sha256); ++index) {
        build_sha256_nonzero |= request.build_sha256[index] != 0U;
    }
    if (request.magic != ROBUSTO_C6_RECOVERY_IDENTITY_MAGIC ||
        request.version != ROBUSTO_C6_RECOVERY_IDENTITY_VERSION ||
        (request.command != ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_READ &&
         request.command != ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM) ||
        (request.command == ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_READ &&
         build_sha256_nonzero) ||
        (request.command == ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM &&
         !build_sha256_nonzero)) {
        ESP_LOGE(TAG, "Invalid identity request fields");
        return;
    }
    if (xQueueSend(request_queue, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Identity request queue is full");
    }
}

static void recovery_task_main(void *context)
{
    robusto_c6_recovery_request_t request;
    (void)context;

    for (;;) {
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Identity request queue receive failed");
            continue;
        }
        esp_err_t error = process_request(&request);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Identity request failed: %s", esp_err_to_name(error));
        }
    }
}

esp_err_t robusto_c6_recovery_init(void)
{
    if (request_queue != NULL || recovery_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    request_queue = xQueueCreate(C6_RECOVERY_REQUEST_QUEUE_CAPACITY,
                                 sizeof(robusto_c6_recovery_request_t));
    if (request_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(recovery_task_main, "robusto_c6_confirm", 4096, NULL, 6,
                    &recovery_task) != pdPASS) {
        vQueueDelete(request_queue);
        request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = robusto_message_frontend_register(
        ROBUSTO_C6_RECOVERY_IDENTITY_REQUEST_MSG_ID, request_callback);
    if (error != ESP_OK) {
        vTaskDelete(recovery_task);
        recovery_task = NULL;
        vQueueDelete(request_queue);
        request_queue = NULL;
        return error;
    }
    return ESP_OK;
}