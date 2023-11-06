#include "robusto_umts_http.h"

#include "esp_netif.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include <robusto_system.h>

char *umts_http_log_prefix;
uint32_t countDataEventCalls = 0;

char bearerToken[74] = CONFIG_ROBUSTO_UMTS_HTTPS_BEARER_TOKEN;

#define HTTP_RECEIVE_BUFFER_SIZE 1024


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    uint8_t output_buffer[HTTP_RECEIVE_BUFFER_SIZE]; // Buffer to store HTTP response

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
        ++countDataEventCalls;
        if (countDataEventCalls%10==0) {
        ROB_LOGI(umts_http_log_prefix, "%d len:%d\n", (int)countDataEventCalls, (int)evt->data_len); }
        dataLenTotal += evt->data_len;
        // Unless bmp.imageOffset initial skip we start reading stream always on byte pointer 0:
        
        // Copy the response into the buffer
        memcpy(output_buffer, evt->data, evt->data_len);

      
        // Hexa dump:
        //ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        countDataEventCalls=0;
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_ON_FINISH\nDownload took: %llu ms\nRefresh and go to sleep %d minutes\n", (r_millis()-startTime)/1000);
        
        break;

    case HTTP_EVENT_DISCONNECTED:
        ROB_LOGI(umts_http_log_prefix, "HTTP_EVENT_DISCONNECTED\n");
        break;
    }
    return ESP_OK;
}


rob_ret_val_t umts_http_post(char *url, uint8_t *data, uint16_t data_len)
{

    // POST Send the IP for logging purpouses
    char post_data[22];
    uint8_t postsize = sizeof(post_data);
    sprintf(post_data, "ip=%s", get_ip_address());

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 9000,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Authentication: Bearer    
    strlcpy(bearerToken, "Bearer: ", sizeof(bearerToken));
    strlcat(bearerToken, CONFIG_ROBUSTO_UMTS_HTTP_BEARER_TOKEN, sizeof(bearerToken));
    
    printf("POST data: %s\n%s\n", post_data, bearerToken);

    esp_http_client_set_header(client, "Authorization", bearerToken);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n",
                 CONFIG_CALE_SCREEN_URL,
                 (int) esp_http_client_get_status_code(client),
                 (int) esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }

}

int umts_http_init(char *_log_prefix)
{
    umts_http_log_prefix = _log_prefix;

}
