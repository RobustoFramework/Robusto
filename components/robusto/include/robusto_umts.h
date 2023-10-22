#pragma once

#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UMTS_LOAD_UMTS

#include <robusto_retval.h>
#include <stdbool.h>


rob_ret_val_t robusto_umts_send_sms(const char *number, const char *message_string);

void robusto_umts_start(char *_log_prefix);
bool robusto_umts_base_up();

void robusto_umts_init(char * _log_prefix);

bool umts_shutdown();

void umts_reset_rtc();


#endif