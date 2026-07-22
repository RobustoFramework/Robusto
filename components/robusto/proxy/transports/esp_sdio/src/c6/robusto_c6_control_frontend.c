#include "robusto_c6_control_frontend.h"

#include "esp_log.h"
#include "robusto_c6_update_protocol.h"
#include "robusto_c6_updater.h"
#include "robusto_message_frontend.h"

static const char *TAG = "c6_control";
static robusto_c6_updater_receive_fn receive_request;

static void update_request_callback(uint32_t message_id,
                                    const uint8_t *data,
                                    size_t data_len)
{
    if (message_id != ROBUSTO_C6_UPDATE_REQUEST_MSG_ID || receive_request == NULL) {
        ESP_LOGE(TAG, "Invalid update envelope");
        return;
    }
    esp_err_t error = receive_request(data, data_len);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Reject update request: %s",
                 esp_err_to_name(error));
    }
}

static esp_err_t start_receive(robusto_c6_updater_receive_fn receive)
{
    if (receive == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    receive_request = receive;
    esp_err_t error = robusto_message_frontend_register(
        ROBUSTO_C6_UPDATE_REQUEST_MSG_ID, update_request_callback);
    if (error != ESP_OK) {
        receive_request = NULL;
    }
    return error;
}

static esp_err_t send_response(const uint8_t *data, size_t data_len)
{
    return robusto_message_frontend_send(ROBUSTO_C6_UPDATE_RESPONSE_MSG_ID,
                                         data, data_len);
}

esp_err_t robusto_c6_control_frontend_init(void)
{
    const robusto_c6_updater_frontend_t frontend = {
        .start_receive = start_receive,
        .send = send_response,
    };
    return robusto_c6_updater_init(&frontend);
}