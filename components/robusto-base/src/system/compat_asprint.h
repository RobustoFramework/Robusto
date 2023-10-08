/**
 * @file compat_asprint.h
 * @author Joseph Werle <joseph.werle@gmail.com>
 * @brief A slightly modified implementation of asprintf, for the Arduino platform
 * @version 0.1
 * @date 2023-03-14
 * 
 * @copyright Copyright (c) 2014 joseph werle <joseph.werle@gmail.com>
 */

#pragma once

#include <stdarg.h>

/**
 * Sets `char **' pointer to be a buffer
 * large enough to hold the formatted string
 * accepting a `va_list' args of variadic
 * arguments.
 */

int
vasprintf (char **, const char *, va_list);

/**
 * Sets `char **' pointer to be a buffer
 * large enough to hold the formatted
 * string accepting `n' arguments of
 * variadic arguments.
 */

int
asprintf (char **, const char *, ...);
