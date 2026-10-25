
#include "tst_media.h"
// TODO: Add copyrights on all these files
#include <unity.h>
#include <string.h>

#include <robusto_media.h>
#include <robusto_message.h>
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

void tst_media_mask_16bit(void)
{
    robusto_media_types mask = (robusto_media_types)(robusto_mt_mock | robusto_mt_halow);

    TEST_ASSERT_EQUAL_UINT16(0x0100U, robusto_mt_halow);
    TEST_ASSERT_EQUAL_UINT16(0x0200U, ROBUSTO_MEDIA_TYPE_LIMIT);
    TEST_ASSERT_EQUAL_UINT16(0x0180U, mask);
    TEST_ASSERT_TRUE((mask & robusto_mt_mock) != 0U);
    TEST_ASSERT_TRUE((mask & robusto_mt_halow) != 0U);
}

void tst_presentation_media_mask_protocol(void)
{
    robusto_peer_t peer = {0};
    robusto_peer_t *host = get_host_peer();
    robusto_media_types saved_host_media = host->supported_media_types;
    uint8_t *message = NULL;
    int message_length;
    const uint32_t payload_offset = ROBUSTO_PREFIX_BYTES + ROBUSTO_CRC_LENGTH +
                                    ROBUSTO_CONTEXT_BYTE_LEN;
    const uint32_t name_length = strlen(host->name) + 1U;

    host->supported_media_types = (robusto_media_types)(saved_host_media | robusto_mt_halow);
    message_length = robusto_make_presentation(&peer, &message, false, presentation_add);
    TEST_ASSERT_TRUE(message_length > 0);
    uint8_t *presentation = message + payload_offset;
    TEST_ASSERT_EQUAL_UINT8(0U, presentation[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)host->supported_media_types, presentation[3]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)host->name[0], presentation[11U + ROBUSTO_MAC_ADDR_LEN]);
    TEST_ASSERT_EQUAL_UINT32(payload_offset + 11U + ROBUSTO_MAC_ADDR_LEN + name_length,
                             (uint32_t)message_length);
    robusto_free(message);

    peer.supported_media_types = robusto_mt_halow;
    message = NULL;
    message_length = robusto_make_presentation(&peer, &message, false, presentation_add);
    TEST_ASSERT_TRUE(message_length > 0);
    presentation = message + payload_offset;
#ifdef CONFIG_ROBUSTO_SUPPORTS_HALOW
    TEST_ASSERT_EQUAL_UINT8(1U, presentation[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(host->supported_media_types >> 8),
                            presentation[11U + ROBUSTO_MAC_ADDR_LEN]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)host->name[0],
                            presentation[12U + ROBUSTO_MAC_ADDR_LEN]);
    TEST_ASSERT_EQUAL_UINT32(payload_offset + 12U + ROBUSTO_MAC_ADDR_LEN + name_length,
                             (uint32_t)message_length);
#else
    TEST_ASSERT_EQUAL_UINT8(0U, presentation[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)host->name[0],
                            presentation[11U + ROBUSTO_MAC_ADDR_LEN]);
    TEST_ASSERT_EQUAL_UINT32(payload_offset + 11U + ROBUSTO_MAC_ADDR_LEN + name_length,
                             (uint32_t)message_length);
#endif
    robusto_free(message);
    host->supported_media_types = saved_host_media;
}
