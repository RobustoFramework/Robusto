#include <robusto_umts.h>
#ifdef CONFIG_ROBUSTO_UMTS_HTTP
#include "robusto_umts_http.h"
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

#ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
#include <robusto_flash.h>
#endif

static char *umts_http_log_prefix;

uint32_t startTime = 0;

#define HTTP_RECEIVE_BUFFER_SIZE 1024

uint8_t *output_buffer; // Buffer to store HTTP response.
uint32_t output_length;
uint32_t countDataEventCalls;
// TODO: Here we need to persist this to flash memory instead.
#ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
char refresh_token[104];
#define SPIFFS_OAUTH_PATH "/spiffs/robusto_oath_creds.txt"
#else
ROB_RTC_DATA_ATTR char refresh_token[104];
#warning "We are using RTC memory for the 104-byte refresh token, if possible, enable and use SPIFFS flash instead."
#endif



char *access_token;
char *device_code;

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
        countDataEventCalls = 0;
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
        if (countDataEventCalls > 0)
        {
            memcpy(output_buffer + output_length, evt->data, evt->data_len);
            output_length = output_length + evt->data_len;
        }
        else
        {
            robusto_free(output_buffer);
            output_buffer = robusto_malloc(HTTP_RECEIVE_BUFFER_SIZE);
            memcpy(output_buffer, evt->data, evt->data_len);
            output_length = evt->data_len;
        }

        countDataEventCalls++;
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

rob_ret_val_t robusto_umts_http_post_form_multipart(char *url, char *req_body, uint32_t req_body_len, char *bearer, char *context_type, char *name, char* parent)
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
        return ROB_FAIL;
    }
    if (!context_type)
    {
        context_type = "text/plain";
    }
    if (bearer)
    {
        char *bearer_header;
        asprintf(&bearer_header, "Bearer %s", bearer);
        esp_http_client_set_header(client, "Authorization", bearer_header);
        ROB_LOGI(umts_http_log_prefix, "Authorization header set to %s", bearer_header);
    }
    else
    {
        ROB_LOGI(umts_http_log_prefix, "robusto_umts_http_post_form_multipart, no bearer");
    }
    if (!name) {
         name = "unnamed_robusto.txt";
    } 
    char * parent_param;
    if (!parent)  {
        parent_param = "";
    } else {
        asprintf(&parent_param, "\r\n  \"parents\": [\"%s\"], ", parent);
    }

    esp_http_client_set_header(client, "Content-Type", "multipart/related; boundary=boundary_robusto");
    esp_http_client_set_header(client, "Content-Length", "multipart/related; boundary=boundary_robusto");
    
    // Create the parts of the post
    
    char *post_data_fmt = "\r\n--boundary_robusto\r\n"
                          "Content-Type: application/json; charset=UTF-8\r\n\r\n"
                          "{%s\r\n  \"name\": \"%s\",\r\n  \"mimeType\": \"%s\"\r\n}\r\n"
                          "--boundary_robusto\r\n"
                          "Content-Type: %s\r\n\r\n";
    char *post_beginning;
    uint16_t post_beginning_length = asprintf(&post_beginning, post_data_fmt, parent_param, name, context_type, context_type);

    char *post_ending = "\r\n--boundary_robusto--";
    uint16_t port_ending_length = strlen(post_ending);
    size_t post_data_len = post_beginning_length + req_body_len + port_ending_length + 1;
    char * content_length;
    asprintf(&content_length, "%u", post_data_len);
    // We proably need to use SPIRAM here
    char *post_data = heap_caps_malloc(post_data_len, MALLOC_CAP_SPIRAM);

    if (post_data == NULL)
    {
        ROB_LOGE(umts_http_log_prefix, "Failed to allocate memory for post data");
        return ROB_FAIL;
    }
    // Build the post data
    memcpy(post_data, post_beginning, post_beginning_length);
    memcpy(post_data + post_beginning_length, req_body, req_body_len);
    memcpy(post_data + post_beginning_length + req_body_len, post_ending, port_ending_length);

    esp_http_client_set_post_field(client, post_data, post_data_len);
    // Setting this after
    esp_http_client_set_header(client, "Content-Length", content_length);

    startTime = r_millis();
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ROB_LOGI(umts_http_log_prefix, "\nURL: %s\nHTTP POST result status = %d, content_length = %d\n",
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
    robusto_free(post_data);
    return ROB_OK;
}

rob_ret_val_t robusto_umts_http_post_form_urlencoded(char *url, char *req_body, uint32_t req_body_len, char *bearer)
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

    //ROB_LOGI(umts_http_log_prefix, "POST body: %.*s\n", req_body_len, req_body);
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

rob_ret_val_t request_device_code()
{
    // Request device and user codes

    char *req_body;
    int req_body_len = asprintf(&req_body, "client_id=%s&scope=%s", CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID, CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_SCOPES);
    robusto_umts_http_post_form_urlencoded(CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_DEVICE_CODE_URL, req_body, req_body_len, NULL);
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

rob_ret_val_t refresh_access_token()
{
    if (strcmp(refresh_token, "") == 0)
    {
        ROB_LOGE(umts_http_log_prefix, "Internal error: refresh_access_token without an refresh_token!");
        return ROB_FAIL;
    }
    char *token_body;
    int token_body_len = asprintf(&token_body, "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
                                  CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID, CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_SECRET, refresh_token);

    ROB_LOGI(umts_http_log_prefix, "Refreshing an an access token.");
    robusto_umts_http_post_form_urlencoded(CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_ACCESS_TOKEN_URL, token_body, token_body_len, NULL);

    cJSON *json = cJSON_ParseWithLength((char *)output_buffer, output_length);
    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");

    
    if (cJSON_IsString(error))
    {
        if (strcmp(error->valuestring, "invalid_grant") == 0) {
            ROB_LOGE(umts_http_log_prefix, "Our grant is invalid or has expired, resetting refresh- and access token and starting over.");
            // We must probably send an SMS or something here.
            strcpy(refresh_token, "");
            #ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
            if (robusto_spiff_remove(SPIFFS_OAUTH_PATH) == ROB_OK) {
                ROB_LOGI(umts_http_log_prefix, "Removed refresh token from flash.");
                // TODO: Notify by SMS that the grant has expired and when a try to renew will happen.
            } else {
                ROB_LOGE(umts_http_log_prefix, "Failed removing refresh token from flash!");
            }
            
            #endif
            request_device_code();
        } else {
            ROB_LOGE(umts_http_log_prefix, "Unhandled error requesting access token: %s", error->valuestring);
        }
        
        cJSON_Delete(json);
        return ROB_FAIL;
    }

    cJSON *access_token_json = cJSON_GetObjectItemCaseSensitive(json, "access_token");

    if ((cJSON_IsString(access_token_json)) && (access_token_json->valuestring != NULL))
    {
        ROB_LOGI(umts_http_log_prefix, "New access token received: %s", access_token_json->valuestring);
        robusto_free(access_token);
        access_token = robusto_malloc(strlen(access_token_json->valuestring) + 1);
        strcpy(access_token, access_token_json->valuestring);
        cJSON_Delete(json);
        return ROB_OK;
    }
    else
    {
        ROB_LOGE(umts_http_log_prefix, "Unhandled response refreshing access token. Response:\n%.*s", (int)output_length, output_buffer);
        cJSON_Delete(json);
        return ROB_FAIL;
    }
}

rob_ret_val_t request_access_token()
{
    if (access_token)
    {
        // Strange in this context
        ROB_LOGW(umts_http_log_prefix, "We are asking for an access token even though we have one, strange, but I let it pass.");
    }
    if (strcmp(refresh_token, "") == 0)
    {
        ROB_LOGI(umts_http_log_prefix, "No refresh token. We need start from the beginning.");
        // TODO: URL encode setting
        if (request_device_code() != ROB_OK)
        {
            ROB_LOGI(umts_http_log_prefix, "Failed getting a device code");
            return ROB_FAIL;
        }
        if (!device_code)
        {
            ROB_LOGE(umts_http_log_prefix, "Internal failure, no device code set after successful registration.");
            return ROB_FAIL;
        }
        // Wait before polling
        r_delay(10000);

        char *token_body;
        int token_body_len = asprintf(&token_body, "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
                                      CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID, CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_SECRET, device_code);
        int retries = 0;
        do
        {
            ROB_LOGI(umts_http_log_prefix, "Polling for an access token.");
            robusto_umts_http_post_form_urlencoded(CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_ACCESS_TOKEN_URL, token_body, token_body_len, NULL);

            cJSON *json = cJSON_ParseWithLength((char *)output_buffer, output_length);
            cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
            if (cJSON_IsString(error))
            {
                if (strcmp(error->valuestring, "authorization_pending") == 0)
                {
                    ROB_LOGI(umts_http_log_prefix, "Server reported authorization_pending. Waiting before retrying");
                    // Need to wait here to not hammer the server, avoiding a "slow down"-respone)
                    // TODO: Send an SMS reminder with a link?
                }
                else
                {
                    ROB_LOGE(umts_http_log_prefix, "Unhandled error requesting access token, will retry: %s", error->valuestring);
                }
                r_delay(10000);
                cJSON_Delete(json);
                retries++;
                continue;
            }

            cJSON *refresh_token_json = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");
            if ((cJSON_IsString(refresh_token_json)) && (refresh_token_json->valuestring != NULL))
            {
                ROB_LOGI(umts_http_log_prefix, "refresh token received: %s", refresh_token_json->valuestring);
                strcpy(refresh_token, refresh_token_json->valuestring);
                #ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
                if (robusto_spiff_write(SPIFFS_OAUTH_PATH, &refresh_token, sizeof(refresh_token)) == ROB_OK) {
                    ROB_LOGI(umts_http_log_prefix, "Write device code to flash: \"%s\"", refresh_token);
                } else {
                    ROB_LOGE(umts_http_log_prefix, "Failed writing device code to flash: \"%s\"", refresh_token);
                }
                #endif
            }
            else
            {
                // Note that we can move on here really, if there is an access token for some reason.
                ROB_LOGE(umts_http_log_prefix, "No refresh token supplied. Response:\n%.*s", (int)output_length, output_buffer);
                return ROB_FAIL;
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
                ROB_LOGE(umts_http_log_prefix, "Unhandled response polling for an access token. Response:\n%.*s", (int)output_length, output_buffer);
                cJSON_Delete(json);
                return ROB_FAIL;
            }
            ROB_LOGE(umts_http_log_prefix, "Undefined state in request_access_token");
        } while (retries < 10);

        if (retries > 9)
        {
            ROB_LOGE(umts_http_log_prefix, "We did not succeed in getting an access token after 10 retries.");
            return ROB_FAIL;
        }
        else
        {
            return ROB_OK;
        }
    }
    else
    {
        return refresh_access_token();
    }
}

rob_ret_val_t robusto_umts_oauth_post_form_multipart(char *url, char *data, uint32_t data_len, char *context_type, char *name, char* parent)
{
    ROB_LOGI(umts_http_log_prefix, "In robusto_umts_http_post_form_multipart, %lu bytes body", data_len);
    if (!access_token)
    {
        if (request_access_token() != ROB_OK)
        {
            ROB_LOGI(umts_http_log_prefix, "Failed acquiring access token.");
            return ROB_FAIL;
        }
    }

    return robusto_umts_http_post_form_multipart(url, data, data_len, access_token, context_type, name, parent);
}

rob_ret_val_t robusto_umts_oauth_post_urlencode(char *url, char *data, uint32_t data_len)
{
    ROB_LOGI(umts_http_log_prefix, "In robusto_umts_oauth_post_urlencode");
    if (!access_token)
    {
        if (request_access_token() != ROB_OK)
        {
            ROB_LOGI(umts_http_log_prefix, "Failed acquiring access token.");
            return ROB_FAIL;
        }
    }

    return robusto_umts_http_post_form_urlencoded(url, data, data_len, access_token);
}

int umts_http_init(char *_log_prefix)
{
    umts_http_log_prefix = _log_prefix;

#ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
    char *buffer = NULL;
    if (robusto_spiff_read(SPIFFS_OAUTH_PATH, &buffer) == ROB_OK) {
        strcpy(refresh_token, buffer);
        ROB_LOGI(umts_http_log_prefix, "Loaded refresh token from flash: \"%s\"", refresh_token);
    } else {
        strcpy(refresh_token, "");   
    }

#else

    if (robusto_is_first_boot())
    {
        strcpy(refresh_token, "");
    }
    else
    {
        ROB_LOGI(umts_http_log_prefix, "Refresh token stored in RTC: \"%s\"", refresh_token);
    }
#endif


    return 0;
}
#endif