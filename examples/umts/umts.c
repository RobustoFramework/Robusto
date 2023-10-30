#include "umts.h"
#if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) || defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT) 


#include <robusto_umts.h>
#include <robusto_logging.h>

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
#include <robusto_conductor.h>
#endif

char * sms_log_prefix;

#if defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS) && (!defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_NUMBER) || !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_MESSAGE))
#error "Both a SMS recipient number and a message must be set."
#endif

void start_umts_example(char * _log_prefix)
{

    #ifdef CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS
    while (!robusto_umts_sms_up()) {
        ROB_LOGI(sms_log_prefix, "Waiting for device to get going so we can send an SMS");
        r_delay(1000);
    }
    r_delay(30000);
    ROB_LOGI(sms_log_prefix, "Trying to send an SMS.");

    robusto_umts_sms_send(CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_NUMBER, CONFIG_ROBUSTO_UMTS_EXAMPLE_SMS_MESSAGE);
    #endif
    #ifdef CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT
    #if !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_TOPIC) || !defined(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_MESSAGE)
        #error "Both an MQTT topic and message must be set"
    #endif
    while (!robusto_umts_mqtt_up()) {
        ROB_LOGI(sms_log_prefix, "Waiting for device to get going so we can send an MQTT message");
        r_delay(1000);
    }
    r_delay(30000);
    ROB_LOGI(sms_log_prefix, "Trying to send an MQTT message.");
    robusto_umts_mqtt_publish(CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_TOPIC, CONFIG_ROBUSTO_UMTS_EXAMPLE_MQTT_MESSAGE);


    #endif
}


void init_umts_example(char * _log_prefix) {
    sms_log_prefix = _log_prefix;
    robusto_umts_init(_log_prefix);
}


#endif