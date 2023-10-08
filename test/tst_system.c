
#include "tst_system.h"
#include <unity.h>

#include <robusto_system.h>

/**
 * @brief Check to that at least 100 milliseconds is returned.
 */
void tst_blink(void)
{
    #if defined(ESP_PLATFORM) || defined(ARDUINO)
    robusto_led_blink(100, 100, 4);
    #else
    robusto_led_blink(1, 1, 2);  // Let's go fast on native
    #endif
}
