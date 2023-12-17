/**
 * @file compat_interrupts_stm32.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief STM32 interrupt handling implementations
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

// TODO: Along with the pulse implementation, this might not be relevant

#if 0

// #ifdef ARDUINO_ARCH_STM32


/*
Note that the implementation assumes that the interrupt pin numbers are mapped to EXTI lines 0 to 15, as is the case for most STM32 microcontrollers. If your specific microcontroller maps interrupt pins to different EXTI lines, you may need to modify the implementation accordingly.
*/

#include "stm32f1xx_hal.h"
#include "stm32f1xx_ll_exti.h"

#define STM32_PIN_COUNT 37

// Interrupt service routine (ISR) function pointer type
typedef void (*ISR)(void);

// ISR function pointers for each EXTI line
static ISR isr_functions[16] = {NULL};

// EXTI line to GPIO pin number mapping
static const uint16_t exti_lines[16] = {
  GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
  GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7,
  GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_11,
  GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15
};

void attachInterrupt(uint8_t pin, ISR isr, int mode) {
  if (pin < STM32_PIN_COUNT && isr != NULL) {
    // Save the ISR function pointer
    isr_functions[pin] = isr;

    // Enable the EXTI interrupt for the corresponding line
    EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line = exti_lines[pin];
    EXTI_InitStruct.Mode = EXTI_MODE_INTERRUPT;
    EXTI_InitStruct.Trigger = (mode == CHANGE) ? EXTI_TRIGGER_BOTH :
                              (mode == FALLING) ? EXTI_TRIGGER_FALLING :
                              EXTI_TRIGGER_RISING;
    EXTI_InitStruct.LineCmd = ENABLE;
    HAL_EXTI_SetConfigLine(&EXTI_InitStruct, EXTI_CONFIG_PORTA);

    // Enable the NVIC interrupt for the corresponding EXTI line
    uint8_t irq_number = digitalPinToInterrupt(pin);
    if (irq_number != 0xFF) {
      HAL_NVIC_SetPriority((IRQn_Type)irq_number, 0, 0);
      HAL_NVIC_EnableIRQ((IRQn_Type)irq_number);
    }
  }
}

void detachInterrupt(uint8_t pin) {
  if (pin < STM32_PIN_COUNT) {
    // Clear the ISR function pointer
    isr_functions[pin] = NULL;

    // Disable the EXTI interrupt for the corresponding line
    EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line = exti_lines[pin];
    EXTI_InitStruct.Mode = EXTI_MODE_IT_DISABLE;
    EXTI_InitStruct.Trigger = EXTI_TRIGGER_NONE;
    EXTI_InitStruct.LineCmd = DISABLE;
    HAL_EXTI_SetConfigLine(&EXTI_InitStruct, EXTI_CONFIG_PORTA);

    // Disable the NVIC interrupt for the corresponding EXTI line
    uint8_t irq_number = digitalPinToInterrupt(pin);
    if (irq_number != 0xFF) {
      HAL_NVIC_DisableIRQ((IRQn_Type)irq_number);
    }
  }
}

// EXTI interrupt handler function
void HAL_GPIO_EXTI_Callback(uint16_t pin) {
  uint8_t exti_line = __HAL_GPIO_EXTI_GET_IT_PIN(pin);
  uint8_t pin_number = 0xFF;
  
  // Find the pin number corresponding to the EXTI line
  for (uint8_t i = 0; i < 16; i++) {
    if (exti_lines[i] == exti_line) {
      pin_number = i;
      break;
    }
  }

  // Call the corresponding ISR function
  if (pin_number < STM32_PIN_COUNT && isr_functions[pin_number] != NULL) {
    isr_functions[pin_number]();
  }
}

uint8_t robusto_digitalPinToInterrupt(uint8_t pin) {
  if (pin < STM32_PIN_COUNT) {
    return (pin == 0) ? EXTI0_IRQn :
           (pin == 1) ? EXTI1_IRQn :
           (pin == 2) ? EXTI2_IRQn :
           (pin == 3) ? EXTI3_IRQn :
           (pin == 4) ? EXTI4_IRQn :
           (pin < 10) ? EXTI9_5_IRQn :
           (pin < 16) ? EXTI15_10_IRQn : 0xFF;
  } else {
    return 0xFF;
  }
}

#endif