#include "robusto_c6_updater.h"

#include <string.h>

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "robusto_c6_update_protocol.h"

static const char *TAG = "robusto_c6_updater";

typedef struct {
    size_t size;
    uint8_t bytes[sizeof(robusto_c6_update_request_t) +
                  ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE];
} update_item_t;

typedef struct {
    uint8_t state;
    uint32_t transaction_id;
    uint32_t total_size;
    uint32_t next_offset;
    uint8_t expected_sha256[32];
    const esp_partition_t *partition;
    esp_ota_handle_t handle;
} update_session_t;

static QueueHandle_t update_queue;
static TaskHandle_t update_task;
static update_item_t callback_item;
static update_item_t worker_item;
static update_session_t session;
static uint8_t hash_buffer[ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE];
static robusto_c6_updater_send_fn send_response;

static esp_err_t calculate_exact_sha256(const esp_partition_t *partition,
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
        esp_err_t error = esp_partition_read(partition, offset, hash_buffer, bytes);
        if (error != ESP_OK) {
            psa_status_t abort_status = psa_hash_abort(&operation);
            if (abort_status != PSA_SUCCESS) {
                ESP_LOGE(TAG, "SHA-256 abort: %ld", (long)abort_status);
            }
            return error;
        }
        status = psa_hash_update(&operation, hash_buffer, bytes);
        if (status != PSA_SUCCESS) {
            psa_status_t abort_status = psa_hash_abort(&operation);
            if (abort_status != PSA_SUCCESS) {
                ESP_LOGE(TAG, "SHA-256 abort: %ld", (long)abort_status);
            }
            return ESP_FAIL;
        }
        offset += bytes;
    }

    status = psa_hash_finish(&operation, digest, 32U, &digest_size);
    if (status != PSA_SUCCESS || digest_size != 32U) {
        if (status != PSA_SUCCESS) {
            psa_status_t abort_status = psa_hash_abort(&operation);
            if (abort_status != PSA_SUCCESS) {
                ESP_LOGE(TAG, "SHA-256 abort: %ld", (long)abort_status);
            }
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void abort_active_update(void)
{
    if (session.state == ROBUSTO_C6_UPDATE_STATE_RECEIVING) {
        esp_err_t error = esp_ota_abort(session.handle);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Abort inactive image: %s",
                     esp_err_to_name(error));
        }
    }
}

#if ROBUSTO_C6_UPDATE_GENERATION_CURRENT == 6
static esp_err_t abort_update(const robusto_c6_update_request_t *request)
{
    if (session.state == ROBUSTO_C6_UPDATE_STATE_IDLE ||
        request->transaction_id != session.transaction_id ||
        request->offset != session.next_offset ||
        request->total_size != session.total_size || request->data_size != 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (session.state == ROBUSTO_C6_UPDATE_STATE_RECEIVING) {
        esp_err_t error = esp_ota_abort(session.handle);
        if (error != ESP_OK) {
            session.state = ROBUSTO_C6_UPDATE_STATE_FAILED;
            return error;
        }
    }
    ESP_LOGI(TAG, "Aborted transaction=%lu bytes=%lu",
             (unsigned long)session.transaction_id,
             (unsigned long)session.next_offset);
    memset(&session, 0, sizeof(session));
    session.state = ROBUSTO_C6_UPDATE_STATE_IDLE;
    return ESP_OK;
}
#endif

static esp_err_t begin_update(const robusto_c6_update_request_t *request)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *partition;
    if (session.state == ROBUSTO_C6_UPDATE_STATE_RECEIVING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request->transaction_id == 0U || request->offset != 0U ||
        request->total_size == 0U || request->data_size != 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || partition == running ||
        request->total_size > partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&session, 0, sizeof(session));
    session.state = ROBUSTO_C6_UPDATE_STATE_IDLE;
    esp_err_t error = esp_ota_begin(partition, request->total_size, &session.handle);
    if (error != ESP_OK) {
        return error;
    }
    session.state = ROBUSTO_C6_UPDATE_STATE_RECEIVING;
    session.transaction_id = request->transaction_id;
    session.total_size = request->total_size;
    session.partition = partition;
    memcpy(session.expected_sha256, request->sha256,
           sizeof(session.expected_sha256));
    ESP_LOGI(TAG, "Begin transaction=%lu target=0x%02x size=%lu",
             (unsigned long)session.transaction_id,
             session.partition->subtype,
             (unsigned long)session.total_size);
    return ESP_OK;
}

static esp_err_t write_update(const robusto_c6_update_request_t *request,
                              const uint8_t *data)
{
    if (session.state != ROBUSTO_C6_UPDATE_STATE_RECEIVING ||
        request->transaction_id != session.transaction_id) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request->offset != session.next_offset || request->data_size == 0U ||
        request->data_size > ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE ||
        request->total_size != session.total_size ||
        request->data_size > session.total_size ||
        request->offset > session.total_size - request->data_size) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = esp_ota_write(session.handle, data, request->data_size);
    if (error != ESP_OK) {
        abort_active_update();
        session.state = ROBUSTO_C6_UPDATE_STATE_FAILED;
        return error;
    }
    session.next_offset += request->data_size;
    return ESP_OK;
}

static esp_err_t end_update(const robusto_c6_update_request_t *request)
{
    uint8_t digest[32];

    if (session.state != ROBUSTO_C6_UPDATE_STATE_RECEIVING ||
        request->transaction_id != session.transaction_id) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request->data_size != 0U || request->offset != session.total_size ||
        request->total_size != session.total_size ||
        session.next_offset != session.total_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = esp_ota_end(session.handle);
    if (error != ESP_OK) {
        session.state = ROBUSTO_C6_UPDATE_STATE_FAILED;
        return error;
    }
    error = calculate_exact_sha256(session.partition, session.total_size, digest);
    if (error != ESP_OK) {
        session.state = ROBUSTO_C6_UPDATE_STATE_FAILED;
        return error;
    }
    if (memcmp(digest, session.expected_sha256, sizeof(digest)) != 0) {
        session.state = ROBUSTO_C6_UPDATE_STATE_FAILED;
        return ESP_ERR_INVALID_CRC;
    }
    session.state = ROBUSTO_C6_UPDATE_STATE_FINALIZED;
    ESP_LOGI(TAG, "Finalized transaction=%lu bytes=%lu target=0x%02x",
             (unsigned long)session.transaction_id,
             (unsigned long)session.next_offset,
             session.partition->subtype);
    return ESP_OK;
}

static esp_err_t activate_update(const robusto_c6_update_request_t *request)
{
    if (session.state != ROBUSTO_C6_UPDATE_STATE_FINALIZED ||
        request->transaction_id != session.transaction_id ||
        request->data_size != 0U || request->offset != session.total_size ||
        request->total_size != session.total_size) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = esp_ota_set_boot_partition(session.partition);
    if (error != ESP_OK) {
        return error;
    }
    session.state = ROBUSTO_C6_UPDATE_STATE_ACTIVATED;
    ESP_LOGI(TAG, "Activated transaction=%lu target=0x%02x",
             (unsigned long)session.transaction_id,
             session.partition->subtype);
    return ESP_OK;
}

static esp_err_t process_request(const robusto_c6_update_request_t *request,
                                 const uint8_t *data)
{
    switch (request->command) {
        case ROBUSTO_C6_UPDATE_COMMAND_STATUS:
            return request->transaction_id == 0U && request->offset == 0U &&
                           request->total_size == 0U && request->data_size == 0U
                       ? ESP_OK
                       : ESP_ERR_INVALID_ARG;
        case ROBUSTO_C6_UPDATE_COMMAND_BEGIN:
            return begin_update(request);
        case ROBUSTO_C6_UPDATE_COMMAND_WRITE:
            return write_update(request, data);
        case ROBUSTO_C6_UPDATE_COMMAND_END:
            return end_update(request);
        case ROBUSTO_C6_UPDATE_COMMAND_ACTIVATE:
            return activate_update(request);
#if ROBUSTO_C6_UPDATE_GENERATION_CURRENT == 6
        case ROBUSTO_C6_UPDATE_COMMAND_ABORT:
            return abort_update(request);
#endif
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t robusto_c6_updater_submit(const uint8_t *data, size_t data_len)
{
    robusto_c6_update_request_t request;

    if (data == NULL || data_len < sizeof(request) ||
        data_len > sizeof(callback_item.bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&request, data, sizeof(request));
    if (request.magic != ROBUSTO_C6_UPDATE_MAGIC ||
        request.version != ROBUSTO_C6_UPDATE_VERSION || request.reserved != 0U ||
        request.reserved2 != 0U ||
        data_len != sizeof(request) + request.data_size) {
        return ESP_ERR_INVALID_ARG;
    }
    callback_item.size = data_len;
    memcpy(callback_item.bytes, data, data_len);
    if (xQueueSend(update_queue, &callback_item, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void update_task_main(void *context)
{
    (void)context;
    for (;;) {
        if (xQueueReceive(update_queue, &worker_item, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Update queue receive");
            continue;
        }
        robusto_c6_update_request_t request;
        memcpy(&request, worker_item.bytes, sizeof(request));
        esp_err_t status = process_request(
            &request,
            worker_item.bytes + sizeof(request));
        robusto_c6_update_response_t response = {
            .magic = ROBUSTO_C6_UPDATE_MAGIC,
            .version = ROBUSTO_C6_UPDATE_VERSION,
            .command = request.command,
            .state = session.state,
            .transaction_id = request.transaction_id,
            .status = status,
            .next_offset = session.next_offset,
            .partition_subtype = session.partition == NULL ? 0U : session.partition->subtype,
            .generation = ROBUSTO_C6_UPDATE_GENERATION_CURRENT,
            .updater_revision = ROBUSTO_C6_UPDATER_REVISION_CURRENT,
            .total_size = session.total_size,
        };
        esp_err_t send_error = send_response((const uint8_t *)&response,
                                             sizeof(response));
        if (send_error != ESP_OK) {
            ESP_LOGE(TAG, "Send update response: %s",
                     esp_err_to_name(send_error));
        }
        if (status == ESP_OK &&
            session.state == ROBUSTO_C6_UPDATE_STATE_ACTIVATED) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }
}

esp_err_t robusto_c6_updater_init(
    const robusto_c6_updater_frontend_t *frontend)
{
    if (frontend == NULL || frontend->start_receive == NULL ||
        frontend->send == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&session, 0, sizeof(session));
    session.state = ROBUSTO_C6_UPDATE_STATE_IDLE;
    send_response = frontend->send;
    update_queue = xQueueCreate(ROBUSTO_C6_UPDATE_QUEUE_CAPACITY,
                                sizeof(update_item_t));
    if (update_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(update_task_main, "c6_update", 6144, NULL, 6,
                    &update_task) != pdPASS) {
        vQueueDelete(update_queue);
        update_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = frontend->start_receive(robusto_c6_updater_submit);
    if (error != ESP_OK) {
        vTaskDelete(update_task);
        update_task = NULL;
        vQueueDelete(update_queue);
        update_queue = NULL;
        send_response = NULL;
        return error;
    }
    ESP_LOGI(TAG, "Updater registered generation=%u queue=%u chunk=%u",
             ROBUSTO_C6_UPDATE_GENERATION_CURRENT,
             ROBUSTO_C6_UPDATE_QUEUE_CAPACITY,
             ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE);
    return ESP_OK;
}