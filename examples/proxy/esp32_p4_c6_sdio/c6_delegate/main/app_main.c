#include "esp_log.h"
#include "robusto_proxy_sdio_c6.h"

static const char *TAG = "robusto_c6_delegate";

void app_main(void)
{
    ESP_ERROR_CHECK(robusto_proxy_sdio_c6_start());
    ESP_LOGI(TAG, "Robusto raw SDIO proxy ready");
}