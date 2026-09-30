
#include "tst_logging.h"
// TODO: Add copyrights on all these files
#include <unity.h>


#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>

static int s_observer_calls;
static rob_log_level_t s_observer_level;
static char s_observer_tag[16];
static char s_observer_message[128];

static void test_log_observer(rob_log_level_t level,
                              const char *tag,
                              const char *message,
                              void *context)
{
    int *context_value = (int *)context;

    s_observer_calls++;
    s_observer_level = level;
    snprintf(s_observer_tag, sizeof(s_observer_tag), "%s", tag);
    snprintf(s_observer_message, sizeof(s_observer_message), "%s", message);
    (*context_value)++;
}

/**
 * @brief Just sends a message to the output, to see that it doesnt crash.
 */
void tst_logging(void)
{
    int context_calls = 0;

    s_observer_calls = 0;
    rob_log_set_observer(test_log_observer, &context_calls);
    ROB_LOGW("TT", "Observer value: %i", 4242);
    TEST_ASSERT_EQUAL_INT(1, s_observer_calls);
    TEST_ASSERT_EQUAL_INT(1, context_calls);
    TEST_ASSERT_EQUAL_INT(ROB_LOG_WARN, s_observer_level);
    TEST_ASSERT_EQUAL_STRING("TT", s_observer_tag);
    TEST_ASSERT_NOT_NULL(strstr(s_observer_message, "Observer value: 4242"));

    rob_log_set_observer(NULL, NULL);
    ROB_LOGW("TT", "Observer removed");
    TEST_ASSERT_EQUAL_INT(1, s_observer_calls);

    ROB_LOGI("TT", "Should say \"1001\" here:  %i", 1001);
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
