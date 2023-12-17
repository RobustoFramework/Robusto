/**
 * @file compat_interrupts_esp32.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief ESP32 interrupt handling implementations
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

#include <robusto_system.h>
#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <rom/gpio.h>

#define NOT_AN_INTERRUPT -1

#ifndef RISING
#define RISING  0x4
#endif

#ifndef FALLING
#define FALLING 0x3
#endif

uint8_t robusto_digitalPinToInterrupt(uint8_t pin) {
  if (pin >= GPIO_PIN_COUNT) {
    return NOT_AN_INTERRUPT;
  }
  return pin;
}
uint8_t digitalPinToGPIO(uint8_t pin) {
  // Check if the pin is within range
  if (pin < GPIO_PIN_COUNT) {
    // Get the GPIO number corresponding to the pin
    switch (pin) {
      case 0:  return GPIO_NUM_0;
      case 1:  return GPIO_NUM_1;
      case 2:  return GPIO_NUM_2;
      case 3:  return GPIO_NUM_3;
      case 4:  return GPIO_NUM_4;
      case 5:  return GPIO_NUM_5;
      case 6:  return GPIO_NUM_6;
      case 7:  return GPIO_NUM_7;
      case 8:  return GPIO_NUM_8;
      case 9:  return GPIO_NUM_9;
      case 10: return GPIO_NUM_10;
      case 11: return GPIO_NUM_11;
      case 12: return GPIO_NUM_12;
      case 13: return GPIO_NUM_13;
      case 14: return GPIO_NUM_14;
      case 15: return GPIO_NUM_15;
      case 16: return GPIO_NUM_16;
      case 17: return GPIO_NUM_17;
      case 18: return GPIO_NUM_18;
      case 19: return GPIO_NUM_19;
      case 21: return GPIO_NUM_21;
      case 22: return GPIO_NUM_22;
      case 23: return GPIO_NUM_23;
      case 25: return GPIO_NUM_25;
      case 26: return GPIO_NUM_26;
      case 27: return GPIO_NUM_27;
      /* Not exposed 
      case 28: return GPIO_NUM_28;
      case 29: return GPIO_NUM_29;
      case 30: return GPIO_NUM_30;
      case 31: return GPIO_NUM_31;
      */
      case 32: return GPIO_NUM_32;
      case 33: return GPIO_NUM_33;
      case 34: return GPIO_NUM_34;
      case 35: return GPIO_NUM_35;
      case 36: return GPIO_NUM_36;
      case 37: return GPIO_NUM_37;
      case 38: return GPIO_NUM_38;
      case 39: return GPIO_NUM_39;
      default: return GPIO_NUM_MAX;
    }
  }

  return GPIO_NUM_MAX;
}
void robusto_attachInterrupt(int pin, void (*isr)(void), int mode) {
  if (pin < 0 || pin >= GPIO_PIN_COUNT) {
    return;
  }
  gpio_num_t gpio_num = digitalPinToGPIO(pin);
  if (gpio_num == GPIO_NUM_MAX) {
    return;
  }
  gpio_pad_select_gpio(gpio_num);
  gpio_set_intr_type(gpio_num, mode == RISING ? GPIO_INTR_POSEDGE : mode == FALLING ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
  gpio_isr_handler_add(gpio_num, isr, (void *)gpio_num);

}

void robusto_detachInterrupt(uint8_t pin){
  if (pin >= GPIO_PIN_COUNT) {
    return;
  }
  gpio_num_t gpio_num = digitalPinToGPIO(pin);
  if (gpio_num == GPIO_NUM_MAX) {
    return;
  }

  gpio_isr_handler_remove(gpio_num);

}
#endif