#pragma once
#include <robconfig.h>
#include "stdbool.h"
#include "esp_netif_types.h"
#include "esp_netif_defaults.h"


extern esp_netif_t *umts_ip_esp_netif;

void umts_ip_init(char * _log_prefix);
int umts_ip_enable_command_mode();
int umts_ip_enable_data_mode();
void umts_ip_cleanup();