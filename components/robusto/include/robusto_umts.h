#pragma once

#include <robconfig.h>
#include <robusto_retval.h>
#include <stdbool.h>

#define ROBUSTO_MQTT_SERVICE_ID 1978U
#define ROBUSTO_SMS_SERVICE_ID 1979U

#ifdef CONFIG_ROBUSTO_UMTS_SERVER
rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(char * topic, char *data);
rob_ret_val_t robusto_umts_oauth_post_form_multipart(char *url, char *data, uint16_t data_len, char *context_type);
rob_ret_val_t robusto_umts_oauth_post_urlencode(char *url, char *data, uint16_t data_len);


bool robusto_umts_sms_up();
bool robusto_umts_mqtt_up();
bool robusto_umts_ip_up();

void robusto_umts_start(char * _log_prefix);
void robusto_umts_init(char * _log_prefix);
bool umts_shutdown();

void umts_reset_rtc();


#endif


#ifdef ROBUSTO_UMTS_CLIENT

rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string);
rob_ret_val_t robusto_umts_mqtt_publish(TBD);
#endif