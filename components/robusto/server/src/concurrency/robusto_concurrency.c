/**
 * @file robusto_concurrency.v
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Functionality for generic handling of concurrency; tasks/threads and their synchronization.
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

#include <robusto_concurrency.h>
#include <robusto_time.h>
#include <robusto_logging.h>
#include <string.h>



bool robusto_waitfor_bool(bool *flag, uint32_t timeout_ms) {
    int starttime = r_millis();
    while ((*flag == false) && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        return false;
    } else {
        return true;
    }
}



bool robusto_waitfor_byte(uint8_t *byte_ptr, uint8_t value, uint32_t timeout_ms) {
    int starttime = r_millis();
    while ((*byte_ptr != value) && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        return false;
    } else {
        return true;
    }
}

bool robusto_waitfor_byte_change(uint8_t *byte_ptr, uint32_t timeout_ms) {
    uint8_t start_value = *byte_ptr;
    int starttime = r_millis();
    while ((*byte_ptr == start_value) && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        return false;
    } else {
        return true;
    }
}

bool robusto_waitfor_uint32_t_change(uint32_t *uint32_t_ptr, uint32_t timeout_ms) {
    uint32_t start_value = *uint32_t_ptr;
    int starttime = r_millis();
    while ((*uint32_t_ptr == start_value) && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        return false;
    } else {
        return true;
    }
}


bool robusto_waitfor_int_change(int *int_ptr, uint32_t timeout_ms) {
    int start_value = *int_ptr;
    int starttime = r_millis();
    while ((*int_ptr == start_value) && (r_millis() < starttime + timeout_ms)) {
        robusto_yield();
    }

    if (r_millis() >= starttime + timeout_ms) {
        return false;
    } else {
        return true;
    }
}