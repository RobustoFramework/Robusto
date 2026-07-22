#include "esp_log.h"
#include "robusto_pubsub_server.h"
#include "robusto_proxy_sdio_c6.h"

#define LARGE_PUBLISH_BYTES (200U * 1024U)

static const char *TAG = "robusto_c6_delegate";

static uint8_t expected_byte(uint32_t offset)
{
    return (uint8_t)((offset * 31U + (offset >> 8U) + 0x5AU) & 0xFFU);
}

static rob_ret_val_t verify_large_publish(uint8_t *data, uint32_t data_length)
{
    if (data == NULL || data_length != LARGE_PUBLISH_BYTES) {
        ESP_LOGE(TAG, "large publish size mismatch: expected=%u actual=%lu",
                 LARGE_PUBLISH_BYTES, (unsigned long)data_length);
        return ROB_ERR_PARSING_FAILED;
    }
    for (uint32_t offset = 0U; offset < data_length; ++offset) {
        if (data[offset] != expected_byte(offset)) {
            ESP_LOGE(TAG,
                     "large publish data mismatch: offset=%lu expected=%02x actual=%02x",
                     (unsigned long)offset, expected_byte(offset), data[offset]);
            return ROB_ERR_PARSING_FAILED;
        }
    }
    ESP_LOGI(TAG, "[PASS] verified %u-byte chunked publish", LARGE_PUBLISH_BYTES);
    return ROB_OK;
}

void app_main(void)
{
    uint32_t topic_hash;

    ESP_ERROR_CHECK(robusto_proxy_sdio_c6_start());
    topic_hash = robusto_pubsub_server_subscribe(
        NULL, verify_large_publish, "proxy.test.large");
    ESP_ERROR_CHECK(topic_hash == 0U ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_LOGI(TAG, "Robusto raw SDIO proxy ready");
}