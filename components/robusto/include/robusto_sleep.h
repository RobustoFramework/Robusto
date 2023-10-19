#ifndef _SLEEP_H_
#define _SLEEP_H_

#include "inttypes.h"
#include "stdbool.h"



void robusto_goto_sleep(uint32_t millisecs);
/**
 * @brief Returns how long we went to sleep last
 * 
 * @return uint32_t 
 */
uint32_t get_last_sleep_duration();

uint32_t robusto_get_total_time_awake();
bool robusto_is_first_boot();
int robusto_get_sleep_count();

bool robusto_sleep_init(char * _log_prefix); 

#endif