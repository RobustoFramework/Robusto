#include "tst_defs.h"

char tst_strings[8] = TST_STRINGS;
char tst_strings_match[11] = "\x28\x01\x00" TST_STRINGS;
char tst_binary[4] = TST_BINARY;
char tst_binary_match[5] = "\x40" TST_BINARY;
char tst_multi_match[15] = "\x60" "\x08\x00" TST_STRINGS TST_BINARY;
char tst_multi_conv_match[17] = "\x70" "\x01\x00" "\x08\x00" TST_STRINGS TST_BINARY;

char tst_peername_0[6] = TST_PEERNAME_0;
char tst_peername_1[6] = TST_PEERNAME_1;
