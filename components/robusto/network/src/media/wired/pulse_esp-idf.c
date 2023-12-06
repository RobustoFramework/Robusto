
/**
 * @file pulse_esp-idf.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP-IDF implementation of the pulse library
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

#include "pulse.h"
#ifdef USE_ESPIDF
#include "driver/timer.h"
// TODO: This is deprecated, should probably use the newer one
#include "driver/gpio.h"
/* PulseIn implementation for ESP-IDF */

unsigned long robusto_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    timer_config_t config;
    config.divider = 80; // 80 MHz timer clock
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = true;
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    gpio_set_direction(pin, GPIO_MODE_INPUT);

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);

    unsigned long startTime;
    unsigned long currTime;
    timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &startTime);

    while(gpio_get_level(pin) != state)
    {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &currTime);
        if((currTime - startTime) > timeout)
        {
            timer_pause(TIMER_GROUP_0, TIMER_0);
            return 0;
        }
    }

    timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &startTime);

    while(gpio_get_level(pin) == state)
    {
        timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &currTime);
        if((currTime - startTime) > timeout)
        {
            timer_pause(TIMER_GROUP_0, TIMER_0);
            return 0;
        }
    }

    unsigned long endTime;
    timer_get_counter_value(TIMER_GROUP_0, TIMER_0, &endTime);

    timer_pause(TIMER_GROUP_0, TIMER_0);

    return (endTime - startTime);
}
#endif