#pragma once
#include <robconfig.h>
#include <robusto_retval.h>

rob_ret_val_t umts_http_post(char * url, uint8_t * data, uint16_t data_len);

int umts_http_init(char * _log_prefix);