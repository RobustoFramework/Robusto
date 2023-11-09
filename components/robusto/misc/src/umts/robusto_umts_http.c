#include "robusto_umts_http.h"
#include <robusto_umts.h>
#include "robusto_umts_ip.h"
#include <robusto_system.h>
#include <robusto_logging.h>

#include "esp_netif.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

char *umts_http_log_prefix;
uint32_t countDataEventCalls = 0;
uint32_t startTime = 0;

#define HTTP_RECEIVE_BUFFER_SIZE 1024

uint8_t *output_buffer; // Buffer to store HTTP response. 


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{

    switch (evt->event_id)
    {
    default:
        break;
    case HTTP_EVENT_ERROR:
        ROB_LOGE(umts_http_log_prefix, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ROB_LOGI(umts_http_log_prefix, "Data: %.*s", evt->data_len, (char *)evt->data);
        // Copy the response into the buffer
        // TODO: Handle If it is a lot of data (and it this is not the first time we are called). Probably use SPIRAM here.
        output_buffer = robusto_malloc(evt->data_len);
        memcpy(output_buffer, evt->data, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        countDataEventCalls = 0;
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_ON_FINISH\nDownload took: %lu ms.", r_millis() - startTime);

        break;

    case HTTP_EVENT_DISCONNECTED:
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_DISCONNECTED\n");
        break;
    }
    return ESP_OK;
}

rob_ret_val_t robusto_umts_http_post_form_urlencoded(char *url, char *req_body, uint16_t req_body_len, char * bearer)
{
        esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 9000,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE,
        .transport_type = HTTP_TRANSPORT_OVER_SSL, // Use HTTPS
        .skip_cert_common_name_check = true,
        .use_global_ca_store = true,
        .crt_bundle_attach = esp_crt_bundle_attach,

    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ROB_LOGE(umts_http_log_prefix, "Client failed to initialize");
    }
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

    if (bearer) {
        char * bearer_header;
        asprintf(&bearer_header, "Bearer %s", bearer);
        esp_http_client_set_header(client, "Authorization", bearer_header);
    }

    esp_http_client_set_post_field(client, req_body, req_body_len);

    ROB_LOGI(umts_http_log_prefix, "POST body: %.*s\n", req_body_len, req_body);
    startTime = r_millis;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ROB_LOGI(umts_http_log_prefix, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n",
                 url,
                 (int)esp_http_client_get_status_code(client),
                 (int)esp_http_client_get_content_length(client));

                 
    }
    else
    {
        ROB_LOGE(umts_http_log_prefix, "\nHTTP POST request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    // Free all stuff.
    robusto_free(req_body);
    return ROB_OK;

}

rob_ret_val_t robusto_umts_http_post(char *url, char *data, uint16_t data_len)
{
    ROB_LOGI(umts_http_log_prefix, "In robusto_umts_http_post");

    
    robusto_umts_http_post_form_urlencoded(url, data, data_len, "");


    return ROB_OK;
}

int umts_http_init(char *_log_prefix)
{
    umts_http_log_prefix = _log_prefix;
    return 0;
}
