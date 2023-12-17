/**
 * @file robusto_system.h
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

#pragma once

#include <robconfig.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <robusto_retval.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* The version of Robusto. TODO: Add something more DevOps:y instead. */
#define ROBUSTO_VERSION "0.1"
/* The length of MAC-addresses in SDP. DO NOT CHANGE.*/
#define ROBUSTO_MAC_ADDR_LEN 6
/* The length of a Robusto relation identifier*/
#define ROBUSTO_RELATION_LEN 4
/* The length, in bytes of the CRC used in Robusto, 4 bytes for CRC32. 
This also the case when using Fletcher16 checksum, the reasons for it are memory constraints, not the two extra bytes sent. */
#define ROBUSTO_CRC_LENGTH 4
/* The number of bytes Robusto internally adds to all messages to accommodate all possible media-specific addressing schemes */
#define ROBUSTO_PREFIX_BYTES 16 

void robusto_led_blink(uint16_t on_delay, uint16_t off_delay, uint16_t count);



/**
 * @brief A proxy for malloc() for Rubusto
 * 
 * @param size Number of bytes to allocate
 * @return void* Pointer to the allocated memory
 */
void * robusto_malloc(size_t size);
/**
 * @brief A proxy for rermalloc() for Rubusto
 * 
 * @param ptr Pointer to the memory to be reallocate
 * @param size  Number of bytes to allocate
 * @return void* Pointer to the re-allocated memory
 */
void * robusto_realloc(void **ptr, size_t size);

/**
 * @brief Deallocates the memory at the pointer
 * 
 * @param ptr Pointer to the memory to be freed
 */
void robusto_free(void *ptr);

/**
 * @brief Calculate checksum according to the Fletcher16-algorithm.
 * 
 * @param data Data to calculate checksum for
 * @param count The number of bytes
 * @param dest A pointer to where to put the Fletcher16 checksum.
 */
void robusto_checksum16(uint8_t *data, int count, uint16_t *dest);

/**
 * @brief Calculate bitwise big-endian Ethernet AUTODIN II CRC32
 * 
 * @param crc Seed value for computation.  ~0 for Ethernet, sometimes 0 for
 *        other uses, or the previous crc32 value if computing incrementally.
 * @param buf Pointer to buffer over which CRC is run
 * @param len Length of buffer
 * @return uint32_t The CRC32 value
 */
uint32_t robusto_crc32(uint32_t crc, const uint8_t *buf, size_t len);


/**
 * @brief 
 * Sets `char **' pointer to be a buffer
 * large enough to hold the formatted string
 * accepting a `va_list' args of variadic
 * arguments.
 * @param str A pointer to a pointer that will point to the new string
 * @param fmt The format string
 * @param va_list A va_list of variadic arguments.
 * @return int The length
 */
int robusto_vasprintf (char **str, const char *fmt, va_list);

 
/**
 * @brief 
 * Sets `str **' pointer to be a buffer
 * large enough to hold the formatted
 * string accepting `n' arguments of
 * variadic arguments.
 * @param str A ponter to a pointer that will point to the new string

 * @param fmt The format string
 * @param ... Input parameters to the fmt string
 * @return int The length
 */
int robusto_asprintf (char **str, const char *fmt, ...);

uint64_t get_free_mem(void);

uint64_t get_free_mem_spi(void);

rob_ret_val_t robusto_gpio_set_level(uint8_t gpio_num, uint8_t value);


bool robusto_gpio_get_level(uint8_t gpio_num);
rob_ret_val_t robusto_gpio_set_direction(uint8_t gpio_num, bool is_output);

void robusto_gpio_set_pullup(int gpio_num, bool pullup);

void robusto_attachInterrupt(int pin, void (*isr)(void), int mode);
void robusto_detachInterrupt(uint8_t pin);
uint8_t robusto_digitalPinToInterrupt(uint8_t pin);

uint8_t *kconfig_mac_to_6_bytes(uint64_t mac_kconfig);
void t_set_gpio_mode(int gpio_num, uint8_t mode);

void robusto_system_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif