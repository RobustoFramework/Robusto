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
#include <robusto_sleep.h>
#include <cJSON.h>

char *umts_http_log_prefix;
uint32_t countDataEventCalls = 0;
uint32_t startTime = 0;

#define HTTP_RECEIVE_BUFFER_SIZE 1024

uint8_t *output_buffer; // Buffer to store HTTP response.
uint32_t output_length;

// Almost filling RTC here, together with the peer memory, this might be too much
RTC_DATA_ATTR char refresh_token[47];
char *device_code;
char *access_token;

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
        output_length = evt->data_len;
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

rob_ret_val_t robusto_umts_http_post_form_urlencoded(char *url, char *req_body, uint16_t req_body_len, char *bearer)
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

    if (bearer)
    {
        char *bearer_header;
        asprintf(&bearer_header, "Bearer %s", bearer);
        esp_http_client_set_header(client, "Authorization", bearer_header);
    }

    esp_http_client_set_post_field(client, req_body, req_body_len);

    ROB_LOGI(umts_http_log_prefix, "POST body: %.*s\n", req_body_len, req_body);
    startTime = r_millis();
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ROB_LOGI(umts_http_log_prefix, "\nURL: %s\nHTTP POST result Status = %d, content_length = %d\n",
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
    // robusto_free(req_body);
    return ROB_OK;
}

rob_ret_val_t request_google_device_code(char *scope_urlencoded)
{
    // Request device and user codes

    char *req_body;
    int req_body_len = asprintf(&req_body, "client_id=%s&scope=%s", CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID, scope_urlencoded);
    robusto_umts_http_post_form_urlencoded("https://oauth2.googleapis.com/device/code", req_body, req_body_len, NULL);
    cJSON *req_json = cJSON_ParseWithLength((char *)output_buffer, output_length);
    cJSON *device_code_json = cJSON_GetObjectItemCaseSensitive(req_json, "device_code");
    if ((!cJSON_IsString(device_code_json)) || (device_code_json->valuestring == NULL))
    {
        ROB_LOGE(umts_http_log_prefix, "No device_code");
        cJSON_Delete(req_json);
        return ROB_FAIL;
    }
    robusto_free(device_code);
    device_code = robusto_malloc(strlen(device_code_json->valuestring) + 1);
    strcpy(device_code, device_code_json->valuestring);

    cJSON *verification_url = cJSON_GetObjectItemCaseSensitive(req_json, "verification_url");
    if ((!cJSON_IsString(verification_url)) || (verification_url->valuestring == NULL))
    {
        ROB_LOGE(umts_http_log_prefix, "No verification_url");
        cJSON_Delete(req_json);
        return ROB_FAIL;
    }

    cJSON *user_code = cJSON_GetObjectItemCaseSensitive(req_json, "user_code");
    if ((!cJSON_IsString(user_code)) || (user_code->valuestring == NULL))
    {
        ROB_LOGE(umts_http_log_prefix, "No user_code");
        cJSON_Delete(req_json);
        return ROB_FAIL;
    }
    ROB_LOGW(umts_http_log_prefix, "Please go to %s and enter the code '%s'", verification_url->valuestring, user_code->valuestring);
    cJSON_Delete(req_json);
    return ROB_OK;
}

rob_ret_val_t *refresh_google_access_token()
{
    // TODO: Implement this
    return ROB_FAIL;
}

rob_ret_val_t *request_google_access_token()
{
    if (access_token)
    {
        // Useful for following, remove later?
        ROB_LOGI(umts_http_log_prefix, "We are asking for an access token even though we have one.");
    }
    bool just_reregistered = false;
    if (strcmp(refresh_token, "") == 0)
    {
        ROB_LOGI(umts_http_log_prefix, "No refresh token. We need start from the beginning.");
        // TODO: Scope hardcoded to google drive, move into setting and urlencode
        if (request_google_device_code("https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file") != ROB_OK)
        {
            ROB_LOGI(umts_http_log_prefix, "Failed getting a device code");
            return NULL;
        }
        else
        {
            r_delay(10000);
            just_reregistered = true;
        }
    }

    if (just_reregistered)
    {
        if (!device_code) {
            ROB_LOGE(umts_http_log_prefix, "NO DEVICE CODE SET? SHOULD NOT BE POSSIBLE");
        }
        char *token_body;
        int token_body_len = asprintf(&token_body, "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
                                      CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID, CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_SECRET, device_code);

        do
        {
            ROB_LOGI(umts_http_log_prefix, "Polling for an access token.");
            robusto_umts_http_post_form_urlencoded("https://oauth2.googleapis.com/token", token_body, token_body_len, NULL);

            cJSON *json = cJSON_ParseWithLength((char *)output_buffer, output_length);
            cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
            if (cJSON_IsString(error))
            {
                if (strcmp(error->valuestring, "authorization_pending") == 0)
                {
                    ROB_LOGI(umts_http_log_prefix, "Server reported authorization_pending. Waiting before retrying");
                    // Need to wait here to not hammer the server, avoiding a "slow down"-respone)
                    // TODO: Send an SMS reminder?
                }
                else
                {
                    ROB_LOGE(umts_http_log_prefix, "Unhandled error requesting access token, will retry: %s", error->valuestring);

                }
                r_delay(10000);
                cJSON_Delete(json);
                continue;

            }

            cJSON *access_token_json = cJSON_GetObjectItemCaseSensitive(json, "access_token");

            if ((cJSON_IsString(access_token_json)) && (access_token_json->valuestring != NULL))
            {
                ROB_LOGI(umts_http_log_prefix, "Access token received: %s", access_token_json->valuestring);
                robusto_free(access_token);
                access_token = robusto_malloc(strlen(access_token_json->valuestring) + 1);
                strcpy(access_token, access_token_json->valuestring);
                cJSON_Delete(json);
                return ROB_OK;
            }
            else
            {
                ROB_LOGE(umts_http_log_prefix, "Unhandled response requesting access token. Response:\n%.*s", (int)output_length, output_buffer);
                cJSON_Delete(json);
                return ROB_OK;
            }
            
        } while (!access_token);
        return ROB_OK;
    }
    else
    {
        return refresh_google_access_token();
    }
}

// TODO: This is neither only google api (?) or just uploading to drive
rob_ret_val_t robusto_umts_oauth_post(char *url, char *data, uint16_t data_len)
{
    ROB_LOGI(umts_http_log_prefix, "In robusto_umts_oauth_post");
    if (!access_token)
    {
        if (request_google_access_token() != ROB_OK) {
            ROB_LOGI(umts_http_log_prefix, "Failed acquiring access token.");
            return ROB_FAIL;
        }
    }
    
    return robusto_umts_http_post_form_urlencoded(url, data, data_len, access_token);
}

int umts_http_init(char *_log_prefix)
{
    umts_http_log_prefix = _log_prefix;
    if (robusto_is_first_boot())
    {
        strcpy(refresh_token, "");
    }
    else
    {
        ROB_LOGI(umts_http_log_prefix, "Stored device code: \"%s\"", refresh_token);
    }
    return 0;
}
