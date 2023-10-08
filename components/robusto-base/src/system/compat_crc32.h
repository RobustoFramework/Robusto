/*
 * crc32.h
 * See c-file for copyright info.
 */

#ifndef _CRC32_H
#define _CRC32_H
#include <inttypes.h>
#include <stdlib.h>
extern int init_crc32(void);
extern void cleanup_crc32(void);
extern uint32_t  crc32_be_no_table(uint32_t crc, uint8_t const *p, size_t len);
extern uint32_t  crc32_be_compat(uint32_t crc, uint8_t const *p, size_t len);
#define ether_crc(length, data)    crc32_be(~0, data, length)
#endif /* _CRC32_H */