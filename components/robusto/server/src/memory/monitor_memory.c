/**
 * @file monitor_memory.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Functionality for memory monitoring 
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robusto_server_init.h>
#ifdef CONFIG_ROBUSTO_MONITOR_MEMORY

#include <robusto_logging.h>
#include <robusto_system.h>
#include <robusto_repeater.h>

//TODO: Add Kconfig settings for all configs. (why are they set here in the first place?)

#define CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH 5

/* Limits for built-in levels levels and warnings. */



/* At what monitor count should the reference memory average be stored?*/
#define CONFIG_ROBUSTO_MONITOR_FIRST_AVG_POINT 7
#if (CONFIG_ROBUSTO_MONITOR_FIRST_AVG_POINT < CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH + 1)
#error "The first average cannot be before the history is populated, i.e. less than its length + 1. \
It is also not recommended to include the first sample as that is taken before system initialisation, \
and thus less helpful for finding memory leaks. "
#endif

static char *memory_monitor_log_prefix;

void monitor_memory_cb();
void monitor_memory_shutdown_cb();

static char _monitor_name[16] = "Memory monitor\x00";

static recurrence_t memory_monitor = {
    recurrence_name : &_monitor_name,
    skip_count : CONFIG_ROBUSTO_MONITOR_MEMORY_SKIP_COUNT,
    skips_left : 0,
    recurrence_callback : &monitor_memory_cb,
    shutdown_callback : &monitor_memory_shutdown_cb
};

/* Statistics */
int sample_count = 0;
/* As this is taken before Robusto initialization it shows the dynamic allocation of Robusto and subsystems */
uint64_t most_memory_available = 0;
/* This is the least memory available at any sample since startup */
uint64_t least_memory_available = 0;
/* This is the first calulated average (first done after CONFIG_ROBUSTO_MONITOR_FIRST_AVG_POINT samples) */
uint64_t first_average_memory_available = 0;

struct history_item
{
    int64_t memory_available;
    // An array of other values (this is dynamically allocated)
    int *other_values;
};
struct history_item history[CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH];

/**
 * @brief The actual sampling of data
 * 
 */
void monitor_memory_cb()
{
    uint64_t curr_mem_avail = get_free_mem();
    int64_t delta_mem_avail = 0;

    if (curr_mem_avail > most_memory_available)
    {
        most_memory_available = curr_mem_avail;
    }
    if (curr_mem_avail < least_memory_available || least_memory_available == 0)
    {
        least_memory_available = curr_mem_avail;
    }

    /* Loop history backwards, aggregate and push everything one step */
    uint64_t agg_avail = curr_mem_avail;
    for (int i = CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH - 1; i > -1; i--)
    {

        agg_avail = agg_avail + history[i].memory_available;

        if (i != 0)
        {
            /* Copy all except from the first to the next */
            history[i] = history[i - 1];
        }
        else
        {

            /* The first is filled with new data */
            history[0].memory_available = curr_mem_avail;
        }
    }

    float avg_mem_avail = 0;
    // In the beginning, we dont have data, and can't do aggregates
    if (sample_count > CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH)
    {
        /* We actually have HISTORY_LENGTH + 1 samples */
        avg_mem_avail = (float)agg_avail / (CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH + 1);
        delta_mem_avail = history[0].memory_available - history[1].memory_available;
    }
    // At a specified point, stored it for future reference
    if (sample_count == CONFIG_ROBUSTO_MONITOR_FIRST_AVG_POINT)
    {
        first_average_memory_available = avg_mem_avail;
    }

    int level = ROB_LOG_DEBUG;
    if ((most_memory_available - curr_mem_avail) > CONFIG_ROBUSTO_MONITOR_DANGER_USAGE)
    {
        ROB_LOGE(memory_monitor_log_prefix, "Dangerously high memory usage at %llu bytes! Will report!(CONFIG_ROBUSTO_MONITOR_DANGER_USAGE=%u)",
                 most_memory_available - curr_mem_avail, CONFIG_ROBUSTO_MONITOR_DANGER_USAGE);
        // TODO: Implement problem callback! And then reboot.
        level = ROB_LOG_ERROR;
    }
    else if ((most_memory_available - curr_mem_avail) > CONFIG_ROBUSTO_MONITOR_WARNING_USAGE)
    {
        ROB_LOGW(memory_monitor_log_prefix, "Unusually high memory usage at %llu bytes(CONFIG_ROBUSTO_MONITOR_WARNING_USAGE=%u).",
                 most_memory_available - curr_mem_avail, CONFIG_ROBUSTO_MONITOR_WARNING_USAGE);
        // TODO: Implement warning callback!
        level = ROB_LOG_WARN;
    }
    sample_count++;
    #ifdef CONFIG_SPIRAM
    ROB_LOG_LEVEL(level, memory_monitor_log_prefix, "Monitor reporting on available resources. Memory:\nCurrently: %llu, avg mem: %.0f bytes. \nDeltas - Avg vs 1st: %.0f, Last vs now: %lli. \nExtremes - Least: %llu, Most(before init): %llu. SPI ram: %llu. ",
                  curr_mem_avail, avg_mem_avail, avg_mem_avail - first_average_memory_available, delta_mem_avail, least_memory_available, most_memory_available, get_free_mem_spi());
    #else
    ROB_LOG_LEVEL(level, memory_monitor_log_prefix, "Monitor reporting on available resources. Memory:\nCurrently: %llu, avg mem: %.0f bytes. \nDeltas - Avg vs 1st: %.0f, Last vs now: %lli. \nExtremes - Least: %llu, Most(before init): %llu. ",
                  curr_mem_avail, avg_mem_avail, avg_mem_avail - first_average_memory_available, delta_mem_avail, least_memory_available, most_memory_available);
    #endif
}

void monitor_memory_shutdown_cb()
{
    ROB_LOGI(memory_monitor_log_prefix, "Shutting down memory monitor. (Nothing to do)");
}
void robusto_memory_monitor_init(char *_log_prefix) {
    memory_monitor_log_prefix = _log_prefix;
    robusto_register_recurrence(&memory_monitor);

    ROB_LOGI(memory_monitor_log_prefix, "Launching memory monitor, history length: %u samples.", CONFIG_ROBUSTO_MONITOR_HISTORY_LENGTH);

}
#endif