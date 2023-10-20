#pragma once
#include <robconfig.h>
#include <stdbool.h>

int umts_mqtt_init(char * _log_prefix);
int publish(char * topic, char * payload, int payload_len);
void umts_mqtt_cleanup();
