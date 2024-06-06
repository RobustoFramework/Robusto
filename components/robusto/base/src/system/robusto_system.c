/**
 * @file robusto_system.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto generic system functions
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

#include <robusto_system.h>
#include <robusto_time.h>

#include <robusto_logging.h>

#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF))
#include "stdlib.h"
#include "stdio.h"
#include <sys/cdefs.h>

#if defined(__APPLE__) 
#include "compat_apple.h"
#endif

#if defined(__linux__)
#include <sys/sysinfo.h>
// TODO: Handle Linux and windows here
//#include <sys/sysinfo.h>
#endif
#endif


#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <rom/crc.h>
#include <nvs_flash.h>
#endif



#include "compat_crc32.h"

#if defined(ARDUINO_ARCH_MBED)
#include "compat_mbed.hpp"

#elif defined(USE_ARDUINO)
#include <Arduino.h>
#endif


char *system_log_prefix = "Unset";

void *robusto_malloc(size_t size)
{
#ifdef USE_ESPIDF
    //return (uint8_t *)malloc(size);
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void *robusto_realloc(void **ptr, size_t size)
{
#ifdef USE_ESPIDF
    return heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
#else
    return realloc(ptr, size);
#endif
}

void robusto_free(void *ptr)
{
    if (ptr != NULL) {
        free(ptr);
    }
    
}

void robusto_checksum16(uint8_t *data, int count, uint16_t *dest)
{
   /* Super-simple moving low-mem checksum, init with a polynomial */
   *dest = 0x8408;
   for(unsigned int index = 0; index < count; ++index )
   {
      *dest+= (uint16_t)*dest + (uint16_t)(*data +index);
   }
}

uint32_t robusto_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    // return crc32_le(crc, buf, len);
    return crc32_be_compat(crc, buf, len);
}
/* TODO: robusto_gpio_set_direction is probably not the best way to do this,
* Both stm32, mbed and arduino does this as a part of the initialization of a gpio, 
* it is likely better to do it like that and call the esp32 gpio_set_direction as part of that instead.
*/
rob_ret_val_t robusto_gpio_set_direction(uint8_t gpio_num, bool is_output) {
    // ROB_LOGI(system_log_prefix, "robusto_gpio_set_direction, gpio_num: %i, is_output: %s.", gpio_num, is_output ? "true":"false" );
#if defined(USE_ESPIDF)
    if (gpio_set_direction(gpio_num, is_output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT) != ESP_OK) {
        
        ROB_LOGE(system_log_prefix, "Failed robusto_gpio_set_direction, gpio_num: %i, direction: %s.", gpio_num, is_output ? "output":"input" );
    };
    ROB_LOGE(system_log_prefix, "Robusto_gpio_set_direction, gpio_num: %i, direction: %s.", gpio_num, is_output ? "output":"input" );
#elif defined(ARDUINO_ARCH_MBED)
    set_gpio_dir(gpio_num, is_output);
#elif defined(USE_ARDUINO)
    pinMode(gpio_num,is_output ? OUTPUT: INPUT);
#else
    printf("I (%lu) Robusto: GPIO %i, direction set to %s\n", r_micros(), gpio_num, is_output ? "output":"input");
#endif
    return ROB_OK;
}

// TODO: Implement this for the rest of the platforms
#if defined(USE_ESPIDF)
void robusto_gpio_set_pullup(int gpio_num, bool pullup) {
    
    #ifdef ARDUINO_ARCH_STM32
    // TODO: Add pullup handling for all Arduino cases
    set_gpio_mode(gpio_num, pullup ? INPUT_PULLUP: INPUT);
    #elif defined(USE_ARDUINO)
    pinMode(gpio_num, , pullup ? INPUT_PULLUP: INPUT);
    #elif defined(USE_ESPIDF)
    esp_err_t retval;
    if (pullup) {
        retval = gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);
    } else {
        retval = gpio_set_pull_mode(gpio_num, GPIO_PULLDOWN_ONLY);
    }
    if (retval != ESP_OK) {
        ROB_LOGE(system_log_prefix, "Failed robusto_gpio_set_pullup, gpio_num: %i, pullup: %s.", gpio_num, pullup ? "pullup":"pulldown" );
        return;
    };
    
    #endif
    ROB_LOGE(system_log_prefix, "robusto_gpio_set_pullup, gpio_num: %i, pullup: %s.", gpio_num, pullup ? "pullup":"pulldown" );
}
#else
#warning "robusto_gpio_set_pullup is not implemented. Do that."
#endif
rob_ret_val_t robusto_gpio_set_level(uint8_t gpio_num, uint8_t value)
{
    // ROB_LOGI(system_log_prefix, "robusto_gpio_set_level, gpio_num: %i, value: %u.", gpio_num, value);
    //robusto_gpio_set_direction(gpio_num, true);

#ifdef USE_ESPIDF
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(gpio_num, value));
#elif defined(ARDUINO_ARCH_MBED)
    set_gpio(gpio_num, value);
#elif defined(ARDUINO_ARCH_STM32)
    HAL_GPIO_WritePin(GPIOB, gpio_num, value);
#elif defined(USE_ARDUINO)
    digitalWrite(gpio_num, value);
#else
    printf("I (%lu) Robusto: GPIO %i, set to %hu\n", r_micros(), gpio_num, value);
#endif
    return ROB_OK;
}

bool robusto_gpio_get_level(uint8_t gpio_num)
{

#ifdef USE_ESPIDF
    return gpio_get_level(gpio_num) == 1 ? true: false;
#elif defined(ARDUINO_ARCH_MBED)
    return get_gpio(gpio_num);
#elif defined(ARDUINO_ARCH_STM32)
    return HAL_GPIO_ReadPin(GPIOB, gpio_num);
#elif defined(USE_ARDUINO)
    return digitalRead(gpio_num);
#elif defined(CONFIG_ROBUSTO_NETWORK_MOCK_TESTING)
    return 1;
#else
    #warning "There is no GPIO implementation on the Native platform, "
    return 0;
#endif
}



void robusto_led_blink(uint16_t on_delay_ms, uint16_t off_delay_ms, uint16_t count) {
    int gpio = CONFIG_ROB_BLINK_GPIO;
    if (gpio < 0) {
        return;
    }
    for (uint16_t i = 0; i <count; i++) {
        robusto_gpio_set_level(gpio, 1);
        r_delay(on_delay_ms);
        robusto_gpio_set_level(gpio, 0);
        r_delay(off_delay_ms);
    }
}
/**
 * @brief Takes a hex value from a Kconfig #define and allocates a six-byte uint8 array.
 * 
 * @param mac_kconfig A number representing a MAC-address
 * @return uint8_t* Pointer to the resulting six byte MAC array
 */
uint8_t *kconfig_mac_to_6_bytes(uint64_t mac_kconfig) {
    #if ROBUSTO_MAC_ADDR_LEN != 6
    #error "This function assumes the length of a MAC to be 6 bytes"
    #endif
    uint8_t *dest = malloc(6);
    dest[0] = (mac_kconfig >> (8*5)) & 0xff;
    dest[1] = (mac_kconfig >> (8*4)) & 0xff;
    dest[2] = (mac_kconfig >> (8*3)) & 0xff;
    dest[3] = (mac_kconfig >> (8*2)) & 0xff;
    dest[4] = (mac_kconfig >> (8)) & 0xff;
    dest[5] = (uint8_t *)mac_kconfig;

    return dest;
}

uint64_t get_free_mem(void) {
#ifdef USE_ESPIDF
    #ifdef CONFIG_SPIRAM
        return heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    #else
        return heap_caps_get_free_size(MALLOC_CAP_8BIT);
    #endif
#elif defined(USE_ARDUINO)
    #warning "This is not implemented on the Arduino platform yet."
    return 0;
#elif defined(__APPLE__) 
    return getAppleFreeMemory();
#elif defined(__linux__)
    struct sysinfo info;
    sysinfo(&info);
    return info.freeram;
#endif
}


uint64_t get_free_mem_spi(void) {
#ifdef USE_ESPIDF
    #ifdef CONFIG_SPIRAM
        return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    #else
        return 0;
    #endif
#elif defined(USE_ARDUINO)
    #warning "Robusto: Free mem for SPI is not implemented for Arduino."
    return 0;
    
#elif defined(__APPLE__) 
    return 0;
#elif defined(__linux__)
    return 0;
#endif
}


void robusto_system_init(char *_log_prefix)
{
    system_log_prefix = _log_prefix;

    init_crc32();
    #if CONFIG_ROB_BLINK_GPIO > -1
    robusto_gpio_set_level(CONFIG_ROB_BLINK_GPIO, 0);
    #endif
    #ifdef USE_ESPIDF
   /* Initialize NVS — it is used to store PHY calibration data and is used by wifi and others */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    #endif
        
}
