#pragma once

static inline void esp_log_stub(const char *tag, const char *format, ...)
{
	(void)tag;
	(void)format;
}

#define ESP_LOGE(...) esp_log_stub(__VA_ARGS__)
#define ESP_LOGW(...) esp_log_stub(__VA_ARGS__)
#define ESP_LOGI(...) esp_log_stub(__VA_ARGS__)