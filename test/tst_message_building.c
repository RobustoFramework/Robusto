#include "tst_message_building.h"

#if !(defined(USE_ARDUINO) || defined(USE_ESPIDF))
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#include <unity.h>
#include <stdbool.h>
#include <string.h>
#include "tst_defs.h"

#include <robusto_system.h>
#include <robusto_message.h>

#include <robusto_logging.h>
#include <inttypes.h>



void tst_make_strings_message(void) {
    uint8_t *tst_strings_res;
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_strings_message input", (uint8_t*)&tst_strings, sizeof(tst_strings));
    int tst_msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 1, 0, (uint8_t *)&tst_strings, sizeof(tst_strings), NULL, NULL, &tst_strings_res);
    rob_log_bit_mesh(ROB_LOG_INFO, "tst_make_strings_message", tst_strings_res, tst_msg_length);
    
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROBUSTO_PREFIX_BYTES +  ROBUSTO_CRC_LENGTH + sizeof(tst_strings_match), tst_msg_length, "The length of the strings message doesn't match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((uint8_t*)&tst_strings_match, tst_strings_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH , tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH, "The strings message content doesn't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        robusto_crc32(0, tst_strings_res  + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, tst_msg_length - ROBUSTO_CRC_LENGTH - ROBUSTO_PREFIX_BYTES),
        *(uint32_t *)(tst_strings_res + ROBUSTO_PREFIX_BYTES), 
        "Strings message CRC32 calculation is incorrect.");
    robusto_free(tst_strings_res);
}
/**
 * @brief Just sends a message to the output, to see that it doesnt crash.
 */
void tst_make_binary_message(void) {
    uint8_t *tst_binary_res;
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_binary_message", (uint8_t*)&tst_binary, sizeof(tst_binary));
    int tst_msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 0, 0, NULL, NULL, (uint8_t*) &tst_binary, sizeof(tst_binary), &tst_binary_res);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + sizeof(tst_binary_match), tst_msg_length, "The length of the binary message doesn't match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((uint8_t*)(&tst_binary_match), tst_binary_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH, "The binary message content doesn't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        robusto_crc32(0, tst_binary_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, tst_msg_length - ROBUSTO_CRC_LENGTH - ROBUSTO_PREFIX_BYTES),
        *(uint32_t *)(tst_binary_res + ROBUSTO_PREFIX_BYTES), 
        "Binary message CRC32 calculation is incorrect.");
    robusto_free(tst_binary_res);
}

/**
 * @brief Make a combined strings and binary message
 * 
 */
void tst_make_multi_message(void) {
    uint8_t *tst_multi_res;
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - strings", (uint8_t*)&tst_strings, sizeof(tst_strings));
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - binary", (uint8_t*)&tst_binary, sizeof(tst_binary));
    int tst_msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 0, 0, 
    (uint8_t*)&tst_strings, sizeof(tst_strings),
    (uint8_t*)&tst_binary, sizeof(tst_binary), 
    &tst_multi_res);
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - result", tst_multi_res, tst_msg_length);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + sizeof(tst_multi_match), tst_msg_length, "The length of the multi message doesn't match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((uint8_t*)&tst_multi_match, tst_multi_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH , tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH, "The multi message content doesn't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        robusto_crc32(0, tst_multi_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH),
        *(uint32_t *)(tst_multi_res + ROBUSTO_PREFIX_BYTES), 
        "Multi-message CRC32 calculation is incorrect.");
    robusto_free(tst_multi_res);
}

/**
 * @brief Create a multi message with a conversation id 
 * 
 */
void tst_make_multi_conversation_message(void){
    uint8_t *tst_multi_res;
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - strings", (uint8_t*)&tst_strings, sizeof(tst_strings));
//    rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - binary", (uint8_t*)&tst_binary, sizeof(tst_binary));
    int tst_msg_length = robusto_make_multi_message_internal(MSG_MESSAGE, 0, 1, 
    (uint8_t*)&tst_strings, sizeof(tst_strings),
    (uint8_t*)&tst_binary, sizeof(tst_binary), 
    &tst_multi_res);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH + sizeof(tst_multi_conv_match), tst_msg_length, "The length of the multi messages doesn't match");
    //rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - multi", (uint8_t*)&tst_multi_conv_match, sizeof(tst_multi_conv_match));
    //rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - result", tst_multi_res, tst_msg_length);
    
    //rob_log_bit_mesh(ROB_LOG_INFO, "test_make_multi_message - crc_calc", &crc_calc, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((uint8_t*)&tst_multi_conv_match, tst_multi_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH , tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH, "The multi message content doesn't match");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
    robusto_crc32(0, tst_multi_res + ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH, tst_msg_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH),
    *(uint32_t *)(tst_multi_res + ROBUSTO_PREFIX_BYTES), 
        "Strings CRC32 calculation is incorrect.");
    //ROB_LOGI("test_make_multi_message", " calc crc32: %"PRIu32", res crc32: %"PRIu32"", crc_calc, *(uint32_t *)tst_multi_res);
    robusto_free(tst_multi_res);
 
}


/**
 * @brief Build null-separated strings
 * 
 */
void tst_build_strings_data(void)
{
    uint8_t *tst_strings_res;
    //rob_log_bit_mesh(ROB_LOG_INFO, "test_make_strings_message input", (uint8_t*)&tst_strings, sizeof(tst_strings));
  
    int tst_strings_length = build_strings_data(&tst_strings_res, "%s|%u", "ABC", 123);
    TEST_ASSERT_EQUAL_INT(8, tst_strings_length);
    //rob_log_bit_mesh(ROB_LOG_INFO, "test_make_strings_message result", tst_strings_res, tst_strings_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&tst_strings, tst_strings_res, tst_strings_length);
    robusto_free(tst_strings_res);
  
}


/**
 * @brief Check crc32
 * 
 */
void tst_calc_message_crc(void) {
    uint32_t crc = robusto_crc32(0, &tst_multi_conv_match, 17);
    TEST_ASSERT_EQUAL_UINT32(3601290074UL, crc);
}


