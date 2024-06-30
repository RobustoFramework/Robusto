/**
 * @file logging_robusto.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto logging functionality.
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

#include "robusto_logging.h"
#if (ROB_LOG_LOCAL_LEVEL > ROB_LOG_NONE)
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ROW_LEN (8 * 9) + 1
#define ASCII_CODE_SPACE 32

void rob_log_write(rob_log_level_t level,
                   const char *tag,
                   const char *format, ...)
{
    va_list list;
    va_start(list, format);
    compat_rob_log_writev(level, tag, format, list);
    va_end(list);
}
#endif

// TODO: Add a Mac address log function

// TODO: Move rob_log_bit_mesh into a macro instead so we don't need this.
void rob_log_bit_mesh(rob_log_level_t level,
                      const char *tag,
                      uint8_t *data, int data_len)
{
    if (ROB_LOG_LOCAL_LEVEL >= level)
    {
        unsigned int curr_pos = 0;
        char str_value[9];
        int value_len;
        char value_row[ROW_LEN];
        char bit_row[ROW_LEN];

        do
        {
            /* Do eight bytes */
            memset(&value_row, 20, ROW_LEN);
            memset(&bit_row, 20, ROW_LEN);
            for (int byte_counter = 0;
                 (byte_counter < 8) && (byte_counter + curr_pos < data_len);
                 byte_counter++)
            {
                /* Make the values row */
                uint8_t curr_byte = data[curr_pos + byte_counter];

                int start_pos = byte_counter + (byte_counter * 8);

                value_len = sprintf(str_value, "%hu(x%.02hx)", curr_byte, curr_byte);
                memcpy(&value_row[start_pos], &str_value, value_len);
                // Fill with spaces, space is 32
                memset(&value_row[start_pos + value_len], ASCII_CODE_SPACE, 9 - value_len);

                /* Make the bits row*/
                for (int bit_counter = 0; bit_counter < 8; bit_counter++)
                {
                    if (curr_byte & 1 << bit_counter)
                    {
                        bit_row[start_pos + bit_counter] = 49;
                    }
                    else
                    {
                        bit_row[start_pos + bit_counter] = 48;
                    }
                }
                bit_row[start_pos + 8] = ASCII_CODE_SPACE;
            }

            // Null-terminate..
            value_row[ROW_LEN - 1] = 0;
            bit_row[ROW_LEN - 1] = 0;
            /* Print values*/
            ROB_LOG_LEVEL(level, tag, "%s", value_row);
            /* Print bits */
            ROB_LOG_LEVEL(level, tag, "%s", bit_row);

            curr_pos = curr_pos + 8;
        } while (curr_pos < data_len);
    }
}
