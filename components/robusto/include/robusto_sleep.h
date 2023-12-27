#pragma once

#include <robconfig.h>

#ifdef CONFIG_ROBUSTO_SLEEP

#include "inttypes.h"

#ifdef USE_NATIVE
#include "stdint.h"
#endif

#include "stdbool.h"

#ifdef __cplusplus
extern "C"
{
#endif

void robusto_goto_sleep(uint32_t millisecs);
/**
 * @brief Returns how long we went to sleep last
 * 
 * @return uint32_t 
 */
uint32_t get_last_sleep_duration();

/**
 * @brief Get total time asleep, awake and milliseconds since starting conducting
 *
 * @return int The number of milliseconds 
 */
uint32_t robusto_sleep_get_time_since_start();

uint32_t robusto_get_total_time_awake();
bool robusto_is_first_boot();
int robusto_get_sleep_count();

bool robusto_sleep_init(char * _log_prefix); 

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif

