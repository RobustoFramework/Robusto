/**
 * @file robusto_init_internal.h
 * @author Nicklas Börjesson (nicklasb@gmail.com)
 * @brief Internal header for all initialization.
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

#ifdef __cplusplus
extern "C"
{
#endif
#include <robconfig.h>

//void robusto_init_compatibility();

/**
 * @brief Register the Network service
 */
void register_network_service();
/**
 * @brief Initialize the server-related functionality
 * @param _log_prefix The log prefix
 */
void robusto_server_init(char * _log_prefix);

/**
 * @brief Initialize the conductor client
 * @param _log_prefix The log prefix
 */
void robusto_conductor_client_init(char *_log_prefix);

/**
 * @brief Initialize the conductor server
 * @param _log_prefix The log prefix
 */
void robusto_conductor_server_init(char *_log_prefix);

/**
 * @brief Register the miscellaneous service
 */
void register_misc_service();

/**
 * @brief Initialize the flash and mount any SPIFFs
 * @note This is done very early in the process as others may depend on this
 * 
 */
void robusto_flash_init(char * _log_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif