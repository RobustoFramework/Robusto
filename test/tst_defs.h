#pragma once

#include <robconfig.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>

#define TST_STRINGS "ABC\000123\000"
#define TST_BINARY "\x00\x00\xFF\xFF"


extern char tst_strings[8];
extern char tst_strings_match[11];
extern char tst_binary[4];
extern char tst_binary_match[5];
extern char tst_multi_match[15];
extern char tst_multi_conv_match[17];
extern char tst_hi[17];


#define ROBUSTO_MAC_ADDRESS_BASE "\x00\x00\x00\x00\x00\x00"

#define TST_RELATIONID_01 0x0CD2F6F9 // CRC32 of 000000000000 000000000001 (first adress 0 and second 1)
#define TST_PEERNAME_0 "PEER0\x00"
extern char tst_peername_0[6];
#define TST_RELATIONID_10 0xDDA2CDDB // CRC32 of 000000000001 000000000000 (first adress 1 and second 0)
#define TST_PEERNAME_1 "PEER1\x00"
extern char tst_peername_1[6];

#ifdef __cplusplus
} /* extern "C" */
#endif