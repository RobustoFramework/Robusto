#include "c6_factory_bootstrap.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"

#define FACTORY_CONNECT_TIMEOUT_MS 30000U
#define FACTORY_OTA_CHUNK_SIZE 1500U

static const char *TAG = "c6_factory_bootstrap";
static StaticSemaphore_t transport_up_storage;
static SemaphoreHandle_t transport_up;
static uint8_t ota_chunk[FACTORY_OTA_CHUNK_SIZE];

int __real_esp_hosted_init(void);

int __wrap_esp_hosted_init(void)
{
    ESP_LOGI(TAG, "Provisioner owns SDIO; deferring ESP-Hosted initialization");
    return ESP_OK;
}

static void hosted_event_callback(void *context,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data)
{
    (void)context;
    (void)event_data;
    if (event_base == ESP_HOSTED_EVENT &&
        event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        xSemaphoreGive(transport_up);
    }
}

static esp_err_t verify_candidate(const esp_partition_t *partition,
                                  const c6_factory_bootstrap_config_t *config)
{
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    uint8_t digest[32];
    size_t digest_size = 0U;
    size_t offset = 0U;
    psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_256);

    if (status != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    while (offset < config->image_size) {
        size_t bytes = config->image_size - offset;
        if (bytes > sizeof(ota_chunk)) {
            bytes = sizeof(ota_chunk);
        }
        esp_err_t error = esp_partition_read(
            partition, config->image_offset + offset, ota_chunk, bytes);
        if (error != ESP_OK) {
            psa_hash_abort(&operation);
            return error;
        }
        status = psa_hash_update(&operation, ota_chunk, bytes);
        if (status != PSA_SUCCESS) {
            psa_hash_abort(&operation);
            return ESP_FAIL;
        }
        offset += bytes;
    }
    status = psa_hash_finish(&operation, digest, sizeof(digest), &digest_size);
    if (status != PSA_SUCCESS || digest_size != sizeof(digest)) {
        return ESP_FAIL;
    }
    return memcmp(digest, config->image_sha256, sizeof(digest)) == 0
               ? ESP_OK
               : ESP_ERR_INVALID_CRC;
}

static esp_err_t transfer_candidate(
    const esp_partition_t *partition,
    const c6_factory_bootstrap_config_t *config)
{
    size_t offset = 0U;
    esp_err_t error = esp_hosted_slave_ota_begin();

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Factory OTA begin: %s", esp_err_to_name(error));
        return error;
    }
    while (error == ESP_OK && offset < config->image_size) {
        size_t bytes = config->image_size - offset;
        if (bytes > sizeof(ota_chunk)) {
            bytes = sizeof(ota_chunk);
        }
        error = esp_partition_read(partition, config->image_offset + offset,
                                   ota_chunk, bytes);
        if (error == ESP_OK) {
            error = esp_hosted_slave_ota_write(ota_chunk, (uint32_t)bytes);
        }
        if (error == ESP_OK) {
            offset += bytes;
            if (offset % (FACTORY_OTA_CHUNK_SIZE * 50U) == 0U ||
                offset == config->image_size) {
                ESP_LOGI(TAG, "Factory OTA sent %u/%u bytes",
                         (unsigned int)offset,
                         (unsigned int)config->image_size);
            }
        }
    }
    if (error != ESP_OK) {
        esp_err_t finalize_error = esp_hosted_slave_ota_end();
        ESP_LOGE(TAG, "Factory OTA failed at %u bytes; finalization=%s",
                 (unsigned int)offset, esp_err_to_name(finalize_error));
        return error;
    }
    error = esp_hosted_slave_ota_end();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Factory OTA end: %s", esp_err_to_name(error));
        return error;
    }
    error = esp_hosted_slave_ota_activate();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Factory OTA activate: %s", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "Factory bootstrap activated");
    return ESP_OK;
}

esp_err_t c6_factory_bootstrap_install(
    const c6_factory_bootstrap_config_t *config)
{
    const esp_partition_t *partition;
    esp_event_handler_instance_t event_instance = NULL;
    esp_hosted_coprocessor_fwver_t version = {0};
    bool hosted_initialized = false;
    bool event_registered = false;
    esp_err_t error;

    if (config == NULL || config->partition_label == NULL ||
        config->image_size == 0U || config->image_sha256 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
        config->partition_label);
    if (partition == NULL || config->image_offset > partition->size ||
        config->image_size > partition->size - config->image_offset) {
        return ESP_ERR_NOT_FOUND;
    }
    error = verify_candidate(partition, config);
    if (error != ESP_OK) {
        return error;
    }
    transport_up = xSemaphoreCreateBinaryStatic(&transport_up_storage);
    if (transport_up == NULL) {
        return ESP_ERR_NO_MEM;
    }
    while (xSemaphoreTake(transport_up, 0) == pdTRUE) {
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    error = esp_event_handler_instance_register(
        ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, hosted_event_callback, NULL,
        &event_instance);
    if (error != ESP_OK) {
        return error;
    }
    event_registered = true;
    error = __real_esp_hosted_init();
    if (error != ESP_OK) {
        goto cleanup;
    }
    hosted_initialized = true;
    error = esp_hosted_connect_to_slave();
    if (error == ESP_OK &&
        xSemaphoreTake(transport_up,
                       pdMS_TO_TICKS(FACTORY_CONNECT_TIMEOUT_MS)) != pdTRUE) {
        error = ESP_ERR_TIMEOUT;
    }
    if (error != ESP_OK) {
        goto cleanup;
    }
    error = esp_hosted_get_coprocessor_fwversion(&version);
    if (error == ESP_FAIL && version.major1 == 0U &&
        version.minor1 == 0U && version.patch1 == 0U) {
        ESP_LOGI(TAG,
                 "Qualified factory pre-version-RPC frontend; installing bootstrap");
        error = transfer_candidate(partition, config);
    } else {
        error = error == ESP_OK ? ESP_ERR_INVALID_VERSION
                                : ESP_ERR_INVALID_RESPONSE;
    }

cleanup:
    if (hosted_initialized) {
        esp_err_t cleanup_error = esp_hosted_deinit();
        if (error == ESP_OK) {
            error = cleanup_error;
        } else if (cleanup_error != ESP_OK) {
            ESP_LOGE(TAG, "ESP-Hosted cleanup: %s",
                     esp_err_to_name(cleanup_error));
        }
    }
    if (event_registered) {
        esp_err_t cleanup_error = esp_event_handler_instance_unregister(
            ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, event_instance);
        if (error == ESP_OK) {
            error = cleanup_error;
        } else if (cleanup_error != ESP_OK) {
            ESP_LOGE(TAG, "Hosted event cleanup: %s",
                     esp_err_to_name(cleanup_error));
        }
    }
    return error;
}
