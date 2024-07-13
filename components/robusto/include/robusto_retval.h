/**
 * @file robusto_retval.h
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Return values functionality.
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
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief The return value of a Robusto call is normally zero or greater if successful, 
 * and a negative return code (see e_robusto_return_codes) if not.
 */
typedef int16_t rob_ret_val_t;

/* Robusto return and status codes */

/* Return code categories/offsets */
#define RBC_GEN -100
/* Communication codes */
#define RBC_COM -200
/* Concurrency (RTOS related) codes */
#define RBC_CC -300

typedef enum e_robusto_return_codes
{

    /* A "return" code of 0x0 means success */
    ROB_OK = 0,

    /* ------ General (RBC_GEN)-------*/

    /* -1 means general failure of an undefined/unknown type */
    ROB_FAIL = RBC_GEN - 1,
    /* An identifier was not found */
    ROB_ERR_INVALID_ID = RBC_GEN - 2,
    /* Invalid input argument */
    ROB_ERR_INVALID_ARG = RBC_GEN - 3,
    /* Out of memory */
    ROB_ERR_OUT_OF_MEMORY = RBC_GEN - 4,
    /* This feature is not supported */
    ROB_ERR_NOT_SUPPORTED = RBC_GEN - 5,
    /* Robusto failed in its initiation. */
    ROB_ERR_INIT_FAIL = RBC_GEN - 6,
    /* The procedure timed out. */
    ROB_ERR_TIMEOUT = RBC_GEN - 7,
    

    /* ------ Communication (RBC_COM)-------*/

    /* A message failed to send for some reason */
    ROB_ERR_SEND_FAIL = RBC_COM - 1,
    /* A one or more messages failed to send during a broadcast */
    ROB_ERR_SEND_SOME_FAIL = RBC_COM - 2,
    /* No message was received */
    ROB_INFO_RECV_NO_MESSAGE = RBC_COM - 3,
    /* No receipt was received within timeout */
    ROB_ERR_NO_RECEIPT = RBC_COM - 4,
    /* The peer doesn't know of we are */
    ROB_ERR_WHO = RBC_COM - 5,

    /* Message to long to comply */
    ROB_ERR_MESSAGE_TOO_LONG = RBC_COM - 10,
    /* Message to short to comply */
    ROB_ERR_MESSAGE_TOO_SHORT = RBC_COM - 11,
    /* Incoming message filtered */
    ROB_ERR_MESSAGE_FILTERED = RBC_COM - 12,
    /* Parsing error */
    ROB_ERR_PARSING_FAILED = RBC_COM - 13,
    /* The wrong CRC */
    ROB_ERR_WRONG_CRC = RBC_COM - 14,
    /* Invalid media */
    ROB_ERR_INVALID_MEDIA = RBC_COM - 15,



    /* There was an error adding a conversation to the conversation list */
    ROB_ERR_CONV_LIST = RBC_COM - 20,
    /* The conversation list is full, too many concurrent conversations. TODO: Add setting? */
    ROB_ERR_CONV_LIST_FULL = RBC_COM - 21,

    /* Peer not found (handle or name not found) */
    ROB_ERR_PEER_NOT_FOUND = RBC_COM - 30,
    /* Peer already exists */
    ROB_ERR_PEER_EXISTS = RBC_COM - 31,
    /* No peers */
    ROB_WARN_NO_PEERS = RBC_COM - 32,
    /* Invalid service id */
    ROB_ERR_INVALID_SERVICE_ID = RBC_COM - 40,   

    /* ------------ Concurrency ------------*/
    /* Couldn't get a semaphore to successfully lock a resource for thread safe usage. */
    ROB_ERR_MUTEX = RBC_CC - 1,
    /* General RTOS error.  ESP32: See enum os_error in os/os_error.h for meaning of values when debugging */
    ROB_ERR_OS_ERROR = RBC_CC - 2,
    /* Queue was full */
    ROB_ERR_QUEUE_FULL = RBC_CC - 3

} e_robusto_return_codes;

#ifdef __cplusplus
} /* extern "C" */
#endif
