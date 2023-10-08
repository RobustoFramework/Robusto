/**
 * @file compat_asprint.h
 * @author Joseph Werle <joseph.werle@gmail.com>
 * @brief A slightly modified implementation of asprintf, for the Arduino platform
 * @version 0.1
 * @date 2023-03-14
 *
 * @copyright Copyright (c) 2014 joseph werle <joseph.werle@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "robusto_system.h"

int robusto_asprintf(char **str, const char *fmt, ...)
{
    int size = 0;
    va_list args;

    // init variadic argumens
    va_start(args, fmt);

#ifdef ARDUINO
    // format and get size
    size = robusto_vasprintf(str, fmt, args);
#else
    size = vasprintf(str, fmt, args);
#endif

    // toss args
    va_end(args);

    return size;
}

int robusto_vasprintf(char **str, const char *fmt, va_list args)
{
    int size = 0;
    va_list tmpa;

    // copy
    va_copy(tmpa, args);

    // apply variadic arguments to
    // sprintf with format to get size
    size = vsnprintf(NULL, 0, fmt, tmpa);

    // toss args
    va_end(tmpa);

    // return -1 to be compliant if
    // size is less than 0
    if (size < 0)
    {
        return -1;
    }

    // alloc with size plus 1 for `\0'
    *str = (char *)robusto_malloc(size + 1);

    // return -1 to be compliant
    // if pointer is `NULL'
    if (NULL == *str)
    {
        return -1;
    }

    // format string with original
    // variadic arguments and set new size
    size = vsprintf(*str, fmt, args);
    return size;
}
