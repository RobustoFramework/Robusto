#pragma once
#include <robconfig.h>
#include "stdbool.h"
#ifdef USE_ESPIDF
#include "esp_netif_types.h"
#include "esp_netif_defaults.h"

extern esp_netif_t *umts_ip_esp_netif;
#endif

char * get_ip_address();

void umts_ip_init(char * _log_prefix);
bool umts_ip_enable_command_mode();
int umts_ip_enable_data_mode();
void umts_ip_cleanup();
