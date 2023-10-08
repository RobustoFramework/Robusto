#pragma once
#include <robconfig.h>
#define TEST_MESSAGE_BUILDING_GROUP 0
/**
 * @brief Test making a message witn only strings
 */
void tst_make_strings_message(void);
/**
 * @brief Test making a message witn only binary data
 */
void tst_make_binary_message(void);
/**
 * @brief Test making a message witn both
 */
void tst_make_multi_message(void);
/**
 * @brief Test making a message witn both and a conversation id
 */
void tst_make_multi_conversation_message(void);
/**
 * @brief Test making building a strings payload
 */
void tst_build_strings_data(void);

/**
 * @brief Test calculating CRC32 
 * 
 */
void tst_calc_message_crc(void);

/**
 * @brief Send a raw data message 
 * 
 */
void tst_message_send_raw(void);