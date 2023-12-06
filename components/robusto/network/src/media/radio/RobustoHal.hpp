/**
 * @file RobustoHal.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto ESP-IDF-compatible HAL for RadioLib 
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

#pragma once
#include <robconfig.h>
// make sure this is always compiled
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <TypeDef.h>

#ifdef USE_ESPIDF
#include <driver/spi_master.h>
#endif

// this file only makes sense for Arduino builds

#include <inttypes.h>

#if defined(RADIOLIB_MBED_TONE_OVERRIDE)
#include "mbed.h"
#endif
// TODO: Needs to fix this include properly
#include <Hal.h>

//#include <SPI.h>

/*!
  \class RobustoHal
  \brief Robusto hardware abstraction for RadioLib.
  This class can be extended to support other Arduino platform or change behaviour of the default implementation.
*/
class RobustoHal : public RadioLibHal {
  public:
    /*!
      \brief Arduino Hal constructor. Will use the default SPI interface and automatically initialize it.
    */
   
    /*!
      \brief Arduino Hal constructor. Will not attempt SPI interface initialization.
      \param spi SPI interface to be used, can also use software SPI implementations.
      \param spiSettings SPI interface settings.
    */
    #ifdef USE_ESPIDF
    RobustoHal(spi_host_device_t host, spi_device_interface_config_t *spi_conf, char * log_prefix);
    #endif
    // implementations of pure virtual RadioLibHal methods
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    void delay(unsigned long ms) override;
    void delayMicroseconds(unsigned long us) override;
    unsigned long millis() override;
    unsigned long micros() override;
    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in);
    void spiEndTransaction() override;
    void spiEnd() override;

    // implementations of virtual RadioLibHal methods
    void init() override;
    void term() override;
    void tone(uint32_t pin, unsigned int frequency, unsigned long duration = 0) override;
    void noTone(uint32_t pin) override;
    void yield() override;
    uint32_t pinToInterrupt(uint32_t pin) override;

#if !defined(RADIOLIB_GODMODE)
  private:
#endif
    #ifdef USE_ESPIDF
    
    spi_host_device_t spi_host;
    spi_device_interface_config_t *spi_conf;
    char *log_prefix;
    spi_device_handle_t spi = NULL;
    #endif
    
    bool initInterface = false;

    #if defined(RADIOLIB_MBED_TONE_OVERRIDE)
    mbed::PwmOut *pwmPin = NULL;
    #endif

    #if defined(ESP32)
    int32_t prev = -1;
    #endif
};

#endif