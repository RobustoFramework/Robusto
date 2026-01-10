/**
 * @file log_esp-idf.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Logging implementation for the ESP-IDF platform.
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
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


#include <robusto_logging.h>
#ifdef USE_ESPIDF
#if ROB_LOG_LOCAL_LEVEL > ROB_LOG_NONE 

#include <esp_log.h>
#include <esp_debug_helpers.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

// Optional ROM printf for drain output; disabled by default to avoid UART in ISR storms.
int esp_rom_printf(const char *fmt, ...);

#ifndef ROB_LOG_ISR_OUTPUT
#define ROB_LOG_ISR_OUTPUT 0
#endif

// Minimal, ISR-friendly formatter that supports %s, %d, %u, %x, %02x, and %%.
static size_t isr_write_uint(char *dst, size_t cap, unsigned int value, int base, bool lower, int min_width, bool zero_pad)
{
    char tmp[16];
    const char *digits = lower ? "0123456789abcdef" : "0123456789ABCDEF";
    int i = 0;
    if (value == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (value && i < (int)sizeof(tmp))
        {
            tmp[i++] = digits[value % base];
            value /= base;
        }
    }

    while (i < min_width && i < (int)sizeof(tmp))
    {
        tmp[i++] = zero_pad ? '0' : ' ';
    }

    size_t written = 0;
    while (i > 0 && written + 1 < cap)
    {
        dst[written++] = tmp[--i];
    }
    return written;
}

static size_t isr_vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    size_t written = 0;
    if (size == 0)
    {
        return 0;
    }

    for (const char *p = fmt; *p && written + 1 < size; ++p)
    {
        if (*p != '%')
        {
            out[written++] = *p;
            continue;
        }

        ++p;
        if (*p == 0)
        {
            break;
        }
        if (*p == '%')
        {
            out[written++] = '%';
            continue;
        }

        bool zero_pad = false;
        int min_width = 0;
        if (*p == '0')
        {
            zero_pad = true;
            ++p;
            if (*p >= '0' && *p <= '9')
            {
                min_width = *p - '0';
                ++p;
            }
        }

        switch (*p)
        {
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            if (s == NULL)
            {
                s = "(null)";
            }
            while (*s && written + 1 < size)
            {
                out[written++] = *s++;
            }
            break;
        }
        case 'd':
        {
            int v = va_arg(ap, int);
            if (v < 0 && written + 1 < size)
            {
                out[written++] = '-';
                v = -v;
            }
            written += isr_write_uint(out + written, size - written, (unsigned int)v, 10, true, min_width, zero_pad);
            break;
        }
        case 'u':
            written += isr_write_uint(out + written, size - written, va_arg(ap, unsigned int), 10, true, min_width, zero_pad);
            break;
        case 'x':
        case 'X':
            written += isr_write_uint(out + written, size - written, va_arg(ap, unsigned int), 16, (*p == 'x'), min_width, zero_pad);
            break;
        default:
            // Unknown specifier: emit it verbatim.
            if (written + 1 < size)
            {
                out[written++] = '%';
            }
            if (written + 1 < size)
            {
                out[written++] = *p;
            }
            break;
        }
    }

    out[written] = '\0';
    return written;
}

#ifndef ROB_LOG_ISR_QUEUE_LEN
#define ROB_LOG_ISR_QUEUE_LEN 16
#endif

#ifndef ROB_LOG_ISR_MAX_MESSAGE
#define ROB_LOG_ISR_MAX_MESSAGE 128
#endif

#ifndef ROB_LOG_ISR_TASK_STACK_WORDS
#define ROB_LOG_ISR_TASK_STACK_WORDS (configMINIMAL_STACK_SIZE * 3)
#endif

#ifndef ROB_LOG_ISR_TASK_PRIORITY
#define ROB_LOG_ISR_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#endif

typedef struct
{
    rob_log_level_t level;
    const char *tag;
    char message[ROB_LOG_ISR_MAX_MESSAGE];
} rob_log_isr_msg_t;

static StaticQueue_t rob_log_isr_queue_struct;
static uint8_t rob_log_isr_queue_storage[ROB_LOG_ISR_QUEUE_LEN * sizeof(rob_log_isr_msg_t)];
static QueueHandle_t rob_log_isr_queue;

static StaticTask_t rob_log_isr_task_tcb;
static StackType_t rob_log_isr_task_stack[ROB_LOG_ISR_TASK_STACK_WORDS];
static TaskHandle_t rob_log_isr_task_handle;

static void rob_log_isr_drain_task(void *arg)
{
    (void)arg;
    rob_log_isr_msg_t msg;
    while (true)
    {
        if (xQueueReceive(rob_log_isr_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
#if ROB_LOG_ISR_OUTPUT
            esp_rom_printf("ISR[%s]: %s\n", msg.tag ? msg.tag : "?", msg.message);
#else
            (void)msg; // Drop silently to avoid UART work.
#endif
        }
    }
}

void rob_log_isr_init(void)
{
    if (rob_log_isr_queue != NULL)
    {
        return;
    }

    rob_log_isr_queue = xQueueCreateStatic(ROB_LOG_ISR_QUEUE_LEN,
                                           sizeof(rob_log_isr_msg_t),
                                           rob_log_isr_queue_storage,
                                           &rob_log_isr_queue_struct);
    if (rob_log_isr_queue == NULL)
    {
        return;
    }

    rob_log_isr_task_handle = xTaskCreateStatic(rob_log_isr_drain_task,
                                                "rob_log_isr",
                                                ROB_LOG_ISR_TASK_STACK_WORDS,
                                                NULL,
                                                ROB_LOG_ISR_TASK_PRIORITY,
                                                rob_log_isr_task_stack,
                                                &rob_log_isr_task_tcb);
    (void)rob_log_isr_task_handle;
}

bool rob_log_isr_enqueue(rob_log_level_t level, const char *tag, const char *format, ...)
{
    if (rob_log_isr_queue == NULL)
    {
        if (!xPortInIsrContext())
        {
            rob_log_isr_init();
        }
        if (rob_log_isr_queue == NULL)
        {
            return false;
        }
    }

    const bool in_isr = xPortInIsrContext();
    // If ISR logging is globally disabled, drop immediately.
    // This is a hard kill-switch to ensure no ISR-side enqueues if UART/RTOS contention persists.
    if (in_isr && ROB_LOG_ISR_OUTPUT == 0)
    {
        return false;
    }
    if (in_isr)
    {
        if (xQueueIsQueueFullFromISR(rob_log_isr_queue))
        {
            return false;
        }
    }
    else if (uxQueueSpacesAvailable(rob_log_isr_queue) == 0)
    {
        return false;
    }

    rob_log_isr_msg_t msg = {
        .level = level,
        .tag = tag,
    };

    va_list list;
    va_start(list, format);
    if (in_isr)
    {
        isr_vsnprintf(msg.message, ROB_LOG_ISR_MAX_MESSAGE, format, list);
    }
    else
    {
        vsnprintf(msg.message, ROB_LOG_ISR_MAX_MESSAGE, format, list);
    }
    va_end(list);

    BaseType_t higher_priority_task_woken = pdFALSE;
    BaseType_t result;
    if (in_isr)
    {
        result = xQueueSendFromISR(rob_log_isr_queue, &msg, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
    else
    {
        result = xQueueSend(rob_log_isr_queue, &msg, 0);
    }

    return result == pdPASS;
}

// Print a stack trace using esp_backtrace_print() in esp_debug_helpers.h
void compat_rob_log_stack_trace(int levels)
{
    esp_backtrace_print(levels);
}

void compat_rob_log_writev(rob_log_level_t level, const char *tag, const char *format, va_list args)
{
    esp_log_writev(level, tag, format, args);
}
// On the ESP platform, there is no need for the sparse mode.
void compat_rob_log_write_sparse(const char *tag, const char *format)
{
    va_list arg_list = {0};
    compat_rob_log_writev(ROB_LOG_LOCAL_LEVEL, tag, format, arg_list);
}
#endif
void r_init_logging()
{
#if ROB_LOG_LOCAL_LEVEL > ROB_LOG_NONE
    rob_log_isr_init();
#endif
}
#endif