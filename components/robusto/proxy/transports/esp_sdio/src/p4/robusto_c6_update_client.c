#include "robusto_c6_update_client.h"

#include <string.h>

#include "esp_log.h"
#include "robusto_c6_update_protocol.h"
#include "robusto_proxy_sdio_host.h"

#define UPDATE_TIMEOUT_MS 30000U
#define UPDATE_TRANSACTION_ID 0x52505258U
#define MAX_UNEXPECTED_RESPONSES 32U

static const char *TAG = "c6_update_client";
static uint8_t request_buffer[sizeof(robusto_c6_update_request_t) +
                              ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE];
static uint8_t response_buffer[ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE];

static esp_err_t receive_expected(uint32_t expected_message_id,
                                  void *response,
                                  size_t expected_response_size)
{
    for (uint32_t attempt = 0U; attempt < MAX_UNEXPECTED_RESPONSES; ++attempt) {
        uint32_t message_id = 0U;
        size_t response_size = 0U;
        esp_err_t error = robusto_proxy_sdio_host_receive(
            &message_id, response_buffer, sizeof(response_buffer),
            &response_size, UPDATE_TIMEOUT_MS);

        if (error != ESP_OK) {
            return error;
        }
        if (message_id != expected_message_id) {
            ESP_LOGW(TAG,
                     "Discard queued response msg=0x%08lx size=%u while waiting for 0x%08lx",
                     (unsigned long)message_id, (unsigned int)response_size,
                     (unsigned long)expected_message_id);
            continue;
        }
        if (response_size != expected_response_size) {
            ESP_LOGE(TAG,
                     "Response 0x%08lx size=%u expected=%u",
                     (unsigned long)message_id, (unsigned int)response_size,
                     (unsigned int)expected_response_size);
            return ESP_ERR_INVALID_RESPONSE;
        }
        memcpy(response, response_buffer, response_size);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Too many queued responses while waiting for 0x%08lx",
             (unsigned long)expected_message_id);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t exchange_update(const robusto_c6_update_request_t *request,
                                 const uint8_t *data,
                                 robusto_c6_update_response_t *response)
{
    size_t request_size = sizeof(*request) + request->data_size;

    if (request == NULL || response == NULL ||
        (request->data_size != 0U && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(request_buffer, request, sizeof(*request));
    if (request->data_size != 0U) {
        memcpy(request_buffer + sizeof(*request), data, request->data_size);
    }
    esp_err_t error = robusto_proxy_sdio_host_send(
        ROBUSTO_C6_UPDATE_REQUEST_MSG_ID, request_buffer, request_size,
        UPDATE_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }
    error = receive_expected(ROBUSTO_C6_UPDATE_RESPONSE_MSG_ID, response,
                             sizeof(*response));
    if (error != ESP_OK) {
        return error;
    }
    if (response->magic != ROBUSTO_C6_UPDATE_MAGIC ||
        response->version != ROBUSTO_C6_UPDATE_VERSION ||
        response->command != request->command ||
        response->transaction_id != request->transaction_id ||
        response->generation != ROBUSTO_C6_UPDATE_GENERATION_CURRENT ||
        response->updater_revision != ROBUSTO_C6_UPDATER_REVISION_CURRENT ||
        response->reserved != 0U) {
        ESP_LOGE(TAG,
                 "Invalid update response: magic=0x%08lx version=%u command=%u/%u transaction=0x%08lx/0x%08lx generation=%u revision=%u reserved=%u",
                 (unsigned long)response->magic, response->version,
                 response->command, request->command,
                 (unsigned long)response->transaction_id,
                 (unsigned long)request->transaction_id,
                 response->generation, response->updater_revision,
                 response->reserved);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return response->status;
}

static esp_err_t exchange_identity(
    uint16_t command,
    const uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE],
    robusto_c6_recovery_record_t *identity)
{
    robusto_c6_recovery_request_t request = {
        .magic = ROBUSTO_C6_RECOVERY_IDENTITY_MAGIC,
        .version = ROBUSTO_C6_RECOVERY_IDENTITY_VERSION,
        .command = command,
    };
    if (identity == NULL ||
        (command == ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM &&
         build_sha256 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (build_sha256 != NULL) {
        memcpy(request.build_sha256, build_sha256,
               sizeof(request.build_sha256));
    }
    esp_err_t error = robusto_proxy_sdio_host_send(
        ROBUSTO_C6_RECOVERY_IDENTITY_REQUEST_MSG_ID, (const uint8_t *)&request,
        sizeof(request), UPDATE_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }
    error = receive_expected(ROBUSTO_C6_RECOVERY_IDENTITY_RECORD_MSG_ID,
                             identity, sizeof(*identity));
    if (error != ESP_OK) {
        return error;
    }
    if (identity->magic != ROBUSTO_C6_RECOVERY_IDENTITY_MAGIC ||
        identity->version != ROBUSTO_C6_RECOVERY_IDENTITY_VERSION ||
        identity->generation != ROBUSTO_C6_UPDATE_GENERATION_CURRENT ||
        identity->protocol_revision != ROBUSTO_C6_RECOVERY_PROTOCOL_REVISION ||
        identity->updater_revision != ROBUSTO_C6_UPDATER_REVISION_CURRENT ||
        identity->transport_frontend != ROBUSTO_C6_RECOVERY_FRONTEND_RAW_SDIO ||
        identity->target_chip != ROBUSTO_C6_RECOVERY_TARGET_ESP32_C6) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t robusto_c6_update_get_identity(
    robusto_c6_recovery_record_t *identity)
{
    return exchange_identity(ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_READ, NULL, identity);
}

esp_err_t robusto_c6_update_confirm_identity(
    const uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE],
    robusto_c6_recovery_record_t *identity)
{
    return exchange_identity(ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM,
                             build_sha256, identity);
}

esp_err_t robusto_c6_update_require_revision_2(void)
{
    const robusto_c6_update_request_t request = {
        .magic = ROBUSTO_C6_UPDATE_MAGIC,
        .version = ROBUSTO_C6_UPDATE_VERSION,
        .command = ROBUSTO_C6_UPDATE_COMMAND_STATUS,
    };
    robusto_c6_update_response_t response;
    esp_err_t error = exchange_update(&request, NULL, &response);
    if (error != ESP_OK) {
        return error;
    }
    return response.state == ROBUSTO_C6_UPDATE_STATE_IDLE &&
                   response.next_offset == 0U && response.total_size == 0U
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
}

esp_err_t robusto_c6_update_install(const esp_partition_t *partition,
                                    size_t partition_offset,
                                    size_t image_size,
                                    const uint8_t sha256[32])
{
    robusto_c6_update_request_t request = {
        .magic = ROBUSTO_C6_UPDATE_MAGIC,
        .version = ROBUSTO_C6_UPDATE_VERSION,
        .command = ROBUSTO_C6_UPDATE_COMMAND_BEGIN,
        .transaction_id = UPDATE_TRANSACTION_ID,
        .total_size = image_size,
    };
    robusto_c6_update_response_t response;

    if (partition == NULL || sha256 == NULL || image_size == 0U ||
        image_size > UINT32_MAX || partition_offset > partition->size ||
        image_size > partition->size - partition_offset) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(request.sha256, sha256, sizeof(request.sha256));
    esp_err_t error = exchange_update(&request, NULL, &response);
    if (error != ESP_OK || response.state != ROBUSTO_C6_UPDATE_STATE_RECEIVING ||
        response.next_offset != 0U || response.total_size != image_size) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    uint32_t offset = 0U;
    while (offset < image_size) {
        size_t bytes = image_size - offset;
        if (bytes > ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE) {
            bytes = ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE;
        }
        error = esp_partition_read(partition, partition_offset + offset,
                                   request_buffer + sizeof(request), bytes);
        if (error != ESP_OK) {
            return error;
        }
        request.command = ROBUSTO_C6_UPDATE_COMMAND_WRITE;
        request.offset = offset;
        request.data_size = bytes;
        error = exchange_update(&request, request_buffer + sizeof(request),
                                &response);
        if (error != ESP_OK ||
            response.state != ROBUSTO_C6_UPDATE_STATE_RECEIVING ||
            response.next_offset != offset + bytes) {
            return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
        }
        offset += bytes;
        if (offset % (ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE * 50U) == 0U ||
            offset == image_size) {
            ESP_LOGI(TAG, "Transferred %lu/%lu bytes", (unsigned long)offset,
                     (unsigned long)image_size);
        }
    }
    request.command = ROBUSTO_C6_UPDATE_COMMAND_END;
    request.offset = image_size;
    request.data_size = 0U;
    error = exchange_update(&request, NULL, &response);
    if (error != ESP_OK || response.state != ROBUSTO_C6_UPDATE_STATE_FINALIZED ||
        response.next_offset != image_size) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    request.command = ROBUSTO_C6_UPDATE_COMMAND_ACTIVATE;
    error = exchange_update(&request, NULL, &response);
    if (error != ESP_OK || response.state != ROBUSTO_C6_UPDATE_STATE_ACTIVATED) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    return ESP_OK;
}