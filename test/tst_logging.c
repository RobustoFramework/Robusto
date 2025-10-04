
#include "tst_logging.h"
// TODO: Add copyrights on all these files
#include <unity.h>


#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>

/**
 * @brief Just sends a message to the output, to see that it doesnt crash.
 */
void tst_logging(void)
{
    
    ROB_LOGI("TT", "Should say \"1001\" here:  %i", 1001);
    
    TEST_ASSERT_TRUE(true);
}
/**
 * @brief Write a more complex log message
 */
void tst_bit_logging(void)
{
    // char data[9] = "ABCDEFGH\0";
    char data[6] = "\x00\x0F\xF0\x0F\xAA\x00";
    rob_log_bit_mesh(ROB_LOG_INFO, "Test_Tag", (uint8_t *)&data, sizeof(data) - 1);

    strcpy(data, "\xFF\xF0\xBC\xBC\xBC\x00");

    rob_log_bit_mesh(ROB_LOG_INFO, "Test_Tag", (uint8_t *)&data, sizeof(data) - 1);

    TEST_ASSERT_TRUE(true);
    //ROB_LOGI("Test_Tag", "After bit logging");
}
