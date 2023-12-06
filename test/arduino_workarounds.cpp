/**
 * @file arduino_workarounds.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief This file contains workarounds for Arduino-based platform variants.
 * @version 0.1
 * @date 2023-04-09
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "arduino_workarounds.hpp"
#if defined(ARDUINO_ARCH_MBED) 

#include <Arduino.h>
#include <unity.h>

REDIRECT_STDOUT_TO(Serial);

void init_arduino_mbed()
{
    // This is only here to show why this file is included. 
    // It is a workaround to be able to configure unity to 
    pinMode(22, OUTPUT);
    digitalWrite(22, 1);
    Serial.begin(9600);
    while(!Serial) delay(10);
    
    Serial.print("HO HO");
    //digitalWrite(22, 0);
}

#endif

#if defined(ARDUINO_ARCH_STM32) 

#include <Arduino.h>
#include "stdio.h"

/**
 * @brief Override putchar for testing, it appears that stm32 doesn't stdout to Serial, or at least SerialUSB.
 * TODO: Check if we should do something like this instead: 
 * https://github.com/platformio/platformio-examples/tree/develop/unit-testing/stm32cube/test
 * 
 * @param c 
 * @return int 
 */

int	putchar (int c) {
    Serial.print((char)c);
    return c;
}

#endif