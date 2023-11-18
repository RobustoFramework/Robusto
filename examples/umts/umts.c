#include "umts.h"
#if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_GOOGLE_DRIVE) 


#include <robusto_umts.h>
#include <robusto_logging.h>

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
#include <robusto_conductor.h>
#endif

static char * umts_log_prefix;

#if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) && (!defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_NUMBER) || !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_MESSAGE))
#error "Both a SMS recipient number and a message must be set."
#endif

void start_umts_example(char * _log_prefix)
{

    #ifdef CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS
    while (!robusto_umts_sms_up()) {
        ROB_LOGI(umts_log_prefix, "Waiting for device to get going so we can send an SMS");
        r_delay(1000);
    }
    r_delay(30000);
    ROB_LOGI(umts_log_prefix, "Trying to send an SMS.");

    robusto_umts_sms_send(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_NUMBER, CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_MESSAGE);
    #endif
    #ifdef CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT
    #if !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_TOPIC) || !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_MESSAGE) || !defined(CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY)
        #error "Both an MQTT topic and message must be set"
    #endif
    while (!robusto_umts_mqtt_up()) {
        ROB_LOGI(umts_log_prefix, "Waiting for device to get going so we can send an MQTT message");
        r_delay(1000);
    }
    ROB_LOGI(umts_log_prefix, "Trying to send an MQTT message.");
    robusto_umts_mqtt_publish(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_TOPIC, CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_MESSAGE);


    #endif
    #ifdef CONFIG_ROBUSTO_UMTS_EXAMPLE_GOOGLE_DRIVE
    #if !defined(CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_ID) || !defined(CONFIG_ROBUSTO_UMTS_HTTP_OAUTH_CLIENT_SECRET) 
        #error "For this example to work, you need to obtain and set the Client ID and client secret for your Google Account. See documentation."
    #endif
    while (!robusto_umts_ip_up()) {
        ROB_LOGI(umts_log_prefix, "Waiting for device to get going so we can send an HTTP message");
        r_delay(1000);
    }
    char * http_file = "Hello file world!";
    r_delay(2000);
    ROB_LOGI(umts_log_prefix, "Call robusto_umts_oauth_post to upload something to google drive");
    robusto_umts_oauth_post_form_multipart("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart", http_file, 18, NULL, "robusto_example_file.txt", NULL);
    
    ROB_LOGI(umts_log_prefix, "After robusto_umts_http_post");
    r_delay(4000);
    #endif

} 


void init_umts_example(char * _log_prefix) {
    umts_log_prefix = _log_prefix;
    
}


#endif