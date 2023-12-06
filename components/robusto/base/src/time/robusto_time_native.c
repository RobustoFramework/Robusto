/**
 * @file time_native.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto time functionality for the native platform (Linux/Windows/MacOS).
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



#include <robusto_time.h>

#if (!defined(USE_ARDUINO)) && (!defined(USE_ESPIDF))
#include <time.h>
#include <stdint.h>
#include <unistd.h>

static clock_t proc_start;

unsigned long r_millis()
{
    if (CLOCKS_PER_SEC == 1000000)
    {
        return (clock() - proc_start) / 1000;
    }
    else
    {
        return (clock() - proc_start) / (CLOCKS_PER_SEC / 1000);
    }
}

unsigned long r_micros()
{
    if (CLOCKS_PER_SEC == 1000000)
    {
        return (clock() - proc_start);
    }
    else
    {
        return (clock() - proc_start) / CLOCKS_PER_SEC * 10000000;
    }
}

void r_delay(unsigned long milliseconds)
{

    // Storing start time
    clock_t start_time = clock();
    clock_t end_time = start_time + (milliseconds * CLOCKS_PER_SEC / 1000);
    
    // looping till required time is achieved
    while (clock() < end_time)
    {
        usleep(10);
    };
}
void r_delay_microseconds(unsigned long microseconds)
{
    usleep(microseconds);
}

void r_init_time()
{
    proc_start = clock();
};

#endif