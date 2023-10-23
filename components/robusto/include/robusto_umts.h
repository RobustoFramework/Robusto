#pragma once

#include <robconfig.h>
#include <robusto_retval.h>
#include <stdbool.h>

#define ROBUSTO_MQTT_SERVICE_ID 1978
#define ROBUSTO_SMS_SERVICE_ID 1979

#ifdef CONFIG_ROBUSTO_UMTS_SERVER
rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(char * topic, char *data);


bool robusto_umts_sms_up();
bool robusto_umts_mqtt_up();

void robusto_umts_start(char * _log_prefix);
void robusto_umts_init(char * _log_prefix);
bool umts_shutdown();

void umts_reset_rtc();


#endif


#ifdef ROBUSTO_UMTS_CLIENT

rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(TBD);
#endif