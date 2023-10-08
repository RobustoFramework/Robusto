
#include "tst_time.h"
// TODO: Add copyrights on all these files
#include <unity.h>

#include <robusto_time.h>

/**
 * @brief Check to that at least 100 milliseconds is returned.
 */
void tst_millis(void)
{
    // TODO: Start with a start time, and measure approximately r_millis() = (starttime + 1000) +/- 100 ms.
    #if defined(ESP_PLATFORM) || defined(ARDUINO)
    r_delay(1010);
    TEST_ASSERT_TRUE((r_millis() > 100));
    #else
    r_delay(100);
    TEST_ASSERT_TRUE((r_millis() > 30)); // Let's go fast on native
    #endif

}
