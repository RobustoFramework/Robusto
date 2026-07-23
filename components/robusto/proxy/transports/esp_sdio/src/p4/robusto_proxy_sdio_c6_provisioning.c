#include "robusto_proxy_sdio_c6_provisioning.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "robusto_c6_update_protocol.h"
#include "robusto_c6_update_client.h"
#include "robusto_proxy_sdio_host.h"

static const char *TAG = "c6_provisioning";
static uint8_t hash_buffer[ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE];

static esp_err_t calculate_partition_sha256(const esp_partition_t *partition,
                                            size_t partition_offset,
                                            size_t size,
                                            uint8_t digest[32])
{
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    size_t digest_size = 0U;
    size_t offset = 0U;
    psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_256);

    if (status != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    while (offset < size) {
        size_t bytes = size - offset;
        if (bytes > sizeof(hash_buffer)) {
            bytes = sizeof(hash_buffer);
        }
        esp_err_t error = esp_partition_read(
            partition, partition_offset + offset, hash_buffer, bytes);
        if (error != ESP_OK) {
            psa_hash_abort(&operation);
            return error;
        }
        status = psa_hash_update(&operation, hash_buffer, bytes);
        if (status != PSA_SUCCESS) {
            psa_hash_abort(&operation);
            return ESP_FAIL;
        }
        offset += bytes;
    }
    status = psa_hash_finish(&operation, digest, 32U, &digest_size);
    return status == PSA_SUCCESS && digest_size == 32U ? ESP_OK : ESP_FAIL;
}

esp_err_t robusto_proxy_sdio_c6_provision(
    const robusto_proxy_sdio_c6_provisioning_config_t *config,
    bool *restart_required,
    bool *identity_received)
{
    const esp_partition_t *candidate;
    robusto_c6_recovery_record_t identity;
    uint8_t candidate_sha256[32];
    esp_err_t error;

    if (config == NULL || config->partition_label == NULL ||
        config->image_size == 0U || config->image_sha256 == NULL ||
        config->elf_sha256 == NULL || restart_required == NULL ||
        identity_received == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *restart_required = false;
    *identity_received = false;
    candidate = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
        config->partition_label);
    if (candidate == NULL || config->image_offset > candidate->size ||
        config->image_size > candidate->size - config->image_offset) {
        return ESP_ERR_NOT_FOUND;
    }
    error = calculate_partition_sha256(candidate, config->image_offset,
                                       config->image_size, candidate_sha256);
    if (error != ESP_OK) {
        return error;
    }
    if (memcmp(candidate_sha256, config->image_sha256,
               sizeof(candidate_sha256)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    error = robusto_proxy_sdio_host_init();
    if (error != ESP_OK) {
        return error;
    }
    error = robusto_c6_update_get_identity(&identity);
    if (error != ESP_OK) {
        esp_err_t cleanup_error = robusto_proxy_sdio_host_deinit();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(TAG, "Raw SDIO cleanup after identity failure: %s",
                     esp_err_to_name(cleanup_error));
            return cleanup_error;
        }
        return error;
    }
    *identity_received = true;
    if (memcmp(identity.build_sha256, config->elf_sha256,
               sizeof(identity.build_sha256)) == 0) {
        if (identity.boot_state == ROBUSTO_C6_RECOVERY_BOOT_PENDING_VERIFY) {
            error = robusto_c6_update_confirm_identity(config->elf_sha256,
                                                       &identity);
            if (error != ESP_OK) {
                goto cleanup;
            }
        }
        if (identity.boot_state != ROBUSTO_C6_RECOVERY_BOOT_CONFIRMED ||
            memcmp(identity.build_sha256, config->elf_sha256,
                   sizeof(identity.build_sha256)) != 0) {
            error = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }
        ESP_LOGI(TAG, "C6 image identity and confirmation match");
        error = ESP_OK;
        goto cleanup;
    }
    error = robusto_c6_update_require_revision_2();
    if (error != ESP_OK) {
        goto cleanup;
    }
    error = robusto_c6_update_install(candidate, config->image_offset,
                                      config->image_size,
                                      config->image_sha256);
    if (error == ESP_OK) {
        *restart_required = true;
    }
cleanup:
    esp_err_t cleanup_error = robusto_proxy_sdio_host_deinit();
    return error == ESP_OK ? cleanup_error : error;
}

esp_err_t robusto_proxy_sdio_c6_confirm_after_activation(
    const robusto_proxy_sdio_c6_provisioning_config_t *config,
    bool *identity_received)
{
    robusto_c6_recovery_record_t identity;
    esp_err_t error;

    if (config == NULL || config->elf_sha256 == NULL ||
        identity_received == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *identity_received = false;
    error = robusto_proxy_sdio_host_init_without_reset();
    if (error != ESP_OK) {
        return error;
    }
    error = robusto_c6_update_get_identity(&identity);
    if (error != ESP_OK) {
        goto cleanup;
    }
    *identity_received = true;
    if (memcmp(identity.build_sha256, config->elf_sha256,
               sizeof(identity.build_sha256)) != 0) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    if (identity.boot_state == ROBUSTO_C6_RECOVERY_BOOT_PENDING_VERIFY) {
        error = robusto_c6_update_confirm_identity(config->elf_sha256,
                                                   &identity);
        if (error != ESP_OK) {
            goto cleanup;
        }
    }
    error = identity.boot_state == ROBUSTO_C6_RECOVERY_BOOT_CONFIRMED &&
                    memcmp(identity.build_sha256, config->elf_sha256,
                           sizeof(identity.build_sha256)) == 0
                ? ESP_OK
                : ESP_ERR_INVALID_STATE;
cleanup:
    esp_err_t cleanup_error = robusto_proxy_sdio_host_deinit();
    return error == ESP_OK ? cleanup_error : error;
}
