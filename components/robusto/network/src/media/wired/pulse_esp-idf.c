
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
#include "driver/gptimer.h"
// TODO: This is deprecated, should probably use the newer one
#include "driver/gpio.h"
/* PulseIn implementation for ESP-IDF */

unsigned long robusto_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 microsecond
    };
    gptimer_new_timer(&timer_config, &gptimer);

    gpio_set_direction(pin, GPIO_MODE_INPUT);

    gptimer_start(gptimer);

    uint64_t startTime;
    uint64_t currTime;
    gptimer_get_raw_count(gptimer, &startTime);

    while(gpio_get_level(pin) != state)
    {
        gptimer_get_raw_count(gptimer, &currTime);
        if((currTime - startTime) > timeout)
        {
            gptimer_stop(gptimer);
            gptimer_del_timer(gptimer);
            return 0;
        }
    }

    gptimer_get_raw_count(gptimer, &startTime);

    while(gpio_get_level(pin) == state)
    {
        gptimer_get_raw_count(gptimer, &currTime);
        if((currTime - startTime) > timeout)
        {
            gptimer_stop(gptimer);
            gptimer_del_timer(gptimer);
            return 0;
        }
    }

    uint64_t endTime;
    gptimer_get_raw_count(gptimer, &endTime);

    gptimer_stop(gptimer);
    gptimer_del_timer(gptimer);

    return (endTime - startTime);
}
#endif