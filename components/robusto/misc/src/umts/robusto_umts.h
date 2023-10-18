#pragma once

#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UMTS_LOAD_UMTS

#include <stdbool.h>

void umts_init(char * _log_prefix);

bool umts_shutdown();

void umts_reset_rtc();


#endif