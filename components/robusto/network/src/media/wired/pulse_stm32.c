

/**
 * @file pulse_stm32.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief STM32 implementation of the pulse library
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
// TODO: Along with the HAL implementation for radiolib, this might not be relevant
#if 0
#ifdef ARDUINO_ARCH_STM32
#include "stm32f1xx_hal.h"
/* PulseIn implementation for STM32. Note that it uses GPIO B and Timer 1, */

//uint32_t robusto_pulseIn(GPIO_TypeDef* GPIOx, uint32_t pin, TIM_HandleTypeDef* htim, uint8_t state, uint32_t timeout)
unsigned long robusto_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_HandleTypeDef TIM_InitStruct;

    // Configure GPIO pin as input
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);

    // Start Timer
    HAL_TIM_Base_Start(htim);

    uint32_t startTime = __HAL_TIM_GET_COUNTER(htim);

    // Wait for pulse to start
    while(HAL_GPIO_ReadPin(GPIOx, pin) != state)
    {
        if((__HAL_TIM_GET_COUNTER(htim) - startTime) > timeout)
        {
            HAL_TIM_Base_Stop(htim);
            return 0;
        }
    }

    startTime = __HAL_TIM_GET_COUNTER(htim);

    // Wait for pulse to end
    while(HAL_GPIO_ReadPin(GPIOx, pin) == state)
    {
        if((__HAL_TIM_GET_COUNTER(htim) - startTime) > timeout)
        {
            HAL_TIM_Base_Stop(htim);
            return 0;
        }
    }

    uint32_t endTime = __HAL_TIM_GET_COUNTER(htim);

    // Stop Timer
    HAL_TIM_Base_Stop(htim);

    return (endTime - startTime);
}
#endif
#endif