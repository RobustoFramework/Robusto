
/**
 * @file spi_hal.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto SPI-HAL
 * @todo Only used by RadioLib HAL, should we move it there?
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
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "driver/spi_master.h"
#ifdef __cplusplus
extern "C"
{
#endif

spi_device_handle_t robusto_spiBegin(spi_host_device_t host, const spi_device_interface_config_t *devcfg);
void robusto_spiBeginTransaction(spi_device_handle_t spi);
void robusto_spiEndTransaction(spi_device_handle_t spi);
uint8_t robusto_spiTransfer_byte(spi_device_handle_t spi, uint8_t data);

void robusto_spiEnd(spi_device_handle_t spi) ;

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif