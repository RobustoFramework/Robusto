
#include "tst_media.h"
// TODO: Add copyrights on all these files
#include <unity.h>

#include <robusto_media.h>
#include <robusto_peer.h>
/**
 * @brief Check to that at least 100 milliseconds is returned.
 */
void tst_add_host_media_type_mock(void)
{

    uint8_t supported_types = robusto_mt_mock;

    #ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
        supported_types |= robusto_mt_espnow;
    #endif
    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        supported_types |= robusto_mt_espnow;
    #endif
    #ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
        supported_types |= robusto_mt_lora;
    #endif

    add_host_supported_media_type(robusto_mt_mock);
    TEST_ASSERT_EQUAL_UINT8(supported_types, get_host_supported_media_types());
 
}
