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

#include "RobustoHal.hpp"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include <robusto_system.h>
#include <robusto_time.h>
#include <robusto_logging.h>
#include <robusto_concurrency.h>

#include "../wired/pulse.h"
#include "../spi/spi_hal.h"
// TODO: Try to use the general variant as a dependency and not like this.
#include "XPowers/init_pmu.hpp"
#include <BuildOpt.h>


#define LOW     0x0
#define HIGH    0x1
#define CHANGE  0x2
#define FALLING 0x3
#define RISING  0x4

#ifdef USE_ESPIDF
RobustoHal::RobustoHal(spi_host_device_t spi_host, spi_device_interface_config_t *spi_conf, char *log_prefix): 
  RadioLibHal(0, 1, LOW, HIGH, RISING, FALLING), spi_host(spi_host), spi_conf(spi_conf), log_prefix(log_prefix), initInterface(true) {}
#endif

void RobustoHal::init() {
  if(initInterface) {
    ROB_LOGI(log_prefix, "Initalizing SPI.");
    spiBegin();
    // TODO: This is not perfect; some versions of the LoRa32 do have a PMU.
    #ifdef CONFIG_LORA_SX126X

    ROB_LOGI(log_prefix, "Initalizing PMU.");
    init_pmu();
    #endif
    
    ROB_LOGI(log_prefix, "Waiting after power on.");
    r_delay(2500);
    initInterface = false;
  }
}

void RobustoHal::term() {
  if(initInterface) {
    spiEnd();
  }
}

void inline RobustoHal::pinMode(uint32_t pin, uint32_t mode) {
  
  // It seems that the only 
  
  if(pin == RADIOLIB_NC) {
    return;
  }
  robusto_gpio_set_direction(pin, mode ? GpioModeOutput : GpioModeInput);
}

void inline RobustoHal::digitalWrite(uint32_t pin, uint32_t value) {
  if(pin == RADIOLIB_NC) {
    return;
  }
  robusto_gpio_set_level(pin, value);
}

uint32_t inline RobustoHal::digitalRead(uint32_t pin) {
  if(pin == RADIOLIB_NC) {
    return 0;
  }
  return robusto_gpio_get_level(pin);
}

void inline RobustoHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
  if(interruptNum == RADIOLIB_NC) {
    return;
  }
  robusto_attachInterrupt(interruptNum, interruptCb, mode);
}

void inline RobustoHal::detachInterrupt(uint32_t interruptNum) {
  if(interruptNum == RADIOLIB_NC) {
    return;
  }
  robusto_detachInterrupt(interruptNum);
}

void inline RobustoHal::delay(unsigned long ms) {
  r_delay(ms);
}

void inline RobustoHal::delayMicroseconds(unsigned long us) {
  r_delay_microseconds(us);
}

unsigned long inline RobustoHal::millis() {
  return r_millis();
}

unsigned long inline RobustoHal::micros() {
  return r_micros();
}

long inline RobustoHal::pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) {
  if(pin == RADIOLIB_NC) {
    return 0;
  }
  return robusto_pulseIn(pin, state, timeout);
}

void inline RobustoHal::spiBegin() {
  spi = robusto_spiBegin(spi_host, spi_conf);
}

void inline RobustoHal::spiBeginTransaction() {
  robusto_spiBeginTransaction(spi);
}

void inline RobustoHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
  for(size_t i = 0; i < len; i++) {
    in[i] = robusto_spiTransfer_byte(spi, out[i]);
  }
}

void inline RobustoHal::spiEndTransaction() {
  robusto_spiEndTransaction(spi);
}

void inline RobustoHal::spiEnd() {
  robusto_spiEnd(spi);
}

void inline RobustoHal::tone(uint32_t pin, unsigned int frequency, unsigned long duration) {
  #if !defined(RADIOLIB_TONE_UNSUPPORTED)
    if(pin == RADIOLIB_NC) {
      return;
    }
  #elif defined(ESP32)
    // ESP32 tone() emulation
    (void)duration;
    if(prev == -1) {
      ledcAttachPin(pin, RADIOLIB_TONE_ESP32_CHANNEL);
    }
    if(prev != frequency) {
      ledcWriteTone(RADIOLIB_TONE_ESP32_CHANNEL, frequency);
    }
    prev = frequency;
  #elif defined(RADIOLIB_MBED_TONE_OVERRIDE)
    // better tone for mbed OS boards
    (void)duration;
    if(!pwmPin) {
      pwmPin = new mbed::PwmOut(digitalPinToPinName(pin));
    }
    pwmPin->period(1.0 / frequency);
    pwmPin->write(0.5);
  
  #elif defined(USE_ARDUINO) 
    ::tone(pin, frequency, duration);
  
  #endif
}

void inline RobustoHal::noTone(uint32_t pin) {
  #if !defined(RADIOLIB_TONE_UNSUPPORTED) and defined(ARDUINO_ARCH_STM32)
    if(pin == RADIOLIB_NC) {
      return;
    }
  #elif !defined(RADIOLIB_TONE_UNSUPPORTED)
    if(pin == RADIOLIB_NC) {
      return;
    }
  #elif defined(ESP32)
    if(pin == RADIOLIB_NC) {
      return;
    }
    // ESP32 tone() emulation
    ledcDetachPin(pin);
    ledcWrite(RADIOLIB_TONE_ESP32_CHANNEL, 0);
    prev = -1;
  #elif defined(RADIOLIB_MBED_TONE_OVERRIDE)
    if(pin == RADIOLIB_NC) {
      return;
    }
    // better tone for mbed OS boards
    (void)pin;
    pwmPin->suspend();
  #elif defined(USE_ARDUINO)
    ::noTone(pin);
  #endif
}

void inline RobustoHal::yield() {
  robusto_yield();
}

uint32_t inline RobustoHal::pinToInterrupt(uint32_t pin) {
  return(robusto_digitalPinToInterrupt(pin));
}

#endif
