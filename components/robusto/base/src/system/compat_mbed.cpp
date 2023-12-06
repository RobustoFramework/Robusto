/**
 * @file compat_mbed.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Mbed compatibility layer
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

#include "compat_mbed.hpp"
#if defined(ARDUINO_ARCH_MBED)
#include "mbed.h"

void set_gpio(int gpio_num, uint8_t value) {
    mbed::DigitalOut pin((PinName)gpio_num, value);
    pin = value;
}

bool get_gpio(int gpio_num) {
    mbed::DigitalIn pin((PinName)gpio_num);
    return pin.read();
}

void set_gpio_dir(int gpio_num, bool is_output) {
    if (is_output) {
        mbed::DigitalOut pin((PinName)gpio_num);
    } else {
        mbed::DigitalIn pin((PinName)gpio_num);
    }
    
}

void set_gpio_mode(int gpio_num, uint8_t mode) {
    mbed::DigitalInOut pin((PinName)gpio_num);
    pin.mode((PinMode)mode);
}

#endif