#pragma once
#include <robconfig.h>
#include "stdbool.h"

#include "esp_netif.h"
#include "esp_modem_dce_config.h"

extern esp_netif_t *umts_ip_esp_netif;

esp_modem_dce_config_t umts_ip_init(char * _log_prefix);
int umts_ip_enable_data_mode();
void umts_ip_cleanup();
