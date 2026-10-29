#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_ROBUSTO_SUPPORTS_HALOW 1
#define CONFIG_ROBUSTO_HALOW_SPI 1
#define CONFIG_ROBUSTO_HALOW_SPI_SCLK_GPIO 43
#define CONFIG_ROBUSTO_HALOW_SPI_MOSI_GPIO 44
#define CONFIG_ROBUSTO_HALOW_SPI_MISO_GPIO 39
#define CONFIG_ROBUSTO_HALOW_SPI_CS_GPIO 42
#define CONFIG_ROBUSTO_HALOW_SPI_IRQ_GPIO 40
#define CONFIG_ROBUSTO_HALOW_SPI_DEBUG_RX_GPIO 12
#define CONFIG_IDF_TARGET_ESP32P4 1
#ifdef CONFIG_ROBUSTO_HALOW_SPI_STA_CONFIGURE
#ifdef TEST_STA_INVALID_SSID
#define CONFIG_ROBUSTO_HALOW_SPI_STA_SSID "01234567890123456789012345678901"
#elif defined(TEST_STA_MAX_SSID)
#define CONFIG_ROBUSTO_HALOW_SPI_STA_SSID "0123456789012345678901234567890"
#else
#define CONFIG_ROBUSTO_HALOW_SPI_STA_SSID "onwatch"
#endif
#ifdef TEST_STA_OPEN
#define CONFIG_ROBUSTO_HALOW_SPI_STA_OPEN 1
#else
#define CONFIG_ROBUSTO_HALOW_SPI_STA_WPA_PSK 1
#endif
/* Synthetic host-test key, never a deployment credential. */
#ifdef TEST_STA_INVALID_PSK
#define CONFIG_ROBUSTO_HALOW_SPI_STA_PSK "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"
#else
#define CONFIG_ROBUSTO_HALOW_SPI_STA_PSK "0123456789ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef"
#endif
#ifdef TEST_STA_INVALID_BANDWIDTH
#define CONFIG_ROBUSTO_HALOW_SPI_STA_BSS_BW 3
#else
#define CONFIG_ROBUSTO_HALOW_SPI_STA_BSS_BW 1
#endif
#ifdef TEST_STA_INVALID_FREQUENCIES
#define CONFIG_ROBUSTO_HALOW_SPI_STA_FREQ_LIST "8655,8655"
#else
#define CONFIG_ROBUSTO_HALOW_SPI_STA_FREQ_LIST "8655"
#endif
#endif
#ifdef CONFIG_ROBUSTO_HALOW_SPI_AP_CONFIGURE
#ifdef TEST_AP_INVALID_SSID
#define CONFIG_ROBUSTO_HALOW_SPI_AP_SSID "01234567890123456789012345678901"
#else
#define CONFIG_ROBUSTO_HALOW_SPI_AP_SSID "0123456789012345678901234567890"
#endif
#define CONFIG_ROBUSTO_HALOW_SPI_AP_PSK "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define CONFIG_ROBUSTO_HALOW_SPI_AP_BSS_BW 2
#define CONFIG_ROBUSTO_HALOW_SPI_AP_FREQ_LIST "9080,9160"
#endif
#define IRAM_ATTR
#define DMA_ATTR
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_INVALID_ARG 5
#define ESP_LOG_INFO 3
#define ESP_LOG_WARN 2
#define ESP_LOG_ERROR 1
typedef int esp_err_t;
static inline const char *esp_err_to_name(int error) { (void)error; return "test"; }
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI test_log
#define ESP_LOGW test_log
#define ESP_LOGE test_log
#define ESP_LOG_LEVEL_LOCAL(level, ...) test_log(__VA_ARGS__)
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, data, length, level) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(tag, data, length, level) ((void)0)

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef struct { unsigned int count; } test_semaphore_t;
typedef test_semaphore_t *SemaphoreHandle_t;
extern test_semaphore_t test_semaphores[2];
extern unsigned int test_semaphore_count, test_uart_calls;
extern unsigned int test_init_calls, test_cleanup_failures;
extern unsigned int test_lock_failures;
extern TickType_t test_ticks;
extern bool test_quiesce_timeout, test_fail_lock_after_exit;
extern bool test_invalid_gpio;
extern TaskHandle_t test_current_task;
void test_finish_receiver(void);
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(test_semaphore_count < 2);
    SemaphoreHandle_t semaphore = &test_semaphores[test_semaphore_count++];
    semaphore->count = 1;
    return semaphore;
}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    SemaphoreHandle_t semaphore = xSemaphoreCreateMutex();
    semaphore->count = 0;
    return semaphore;
}
static inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    (void)timeout;
    if (semaphore == &test_semaphores[0] && test_lock_failures != 0) {
        --test_lock_failures;
        return 0;
    }
    if (semaphore == &test_semaphores[1] && semaphore->count == 0 &&
        !test_quiesce_timeout) {
        test_finish_receiver();
    }
    if (semaphore->count == 0) return 0;
    --semaphore->count;
    if (semaphore == &test_semaphores[1] && test_fail_lock_after_exit) {
        test_fail_lock_after_exit = false;
        test_lock_failures = 1;
    }
    return pdTRUE;
}
static inline int xSemaphoreGive(SemaphoreHandle_t semaphore)
{ assert(semaphore->count == 0); semaphore->count = 1; return pdTRUE; }
static inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { (void)semaphore; }
static inline TickType_t xTaskGetTickCount(void) { return test_ticks; }
void test_receive_delay(void);
static inline void vTaskDelay(TickType_t ticks)
{ test_ticks += ticks; test_receive_delay(); }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return test_current_task; }
static inline int xTaskCreate(void (*task)(void *), const char *name,
                              unsigned int stack, void *argument, unsigned int priority,
                              TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)argument; (void)priority;
    *handle = (void *)(uintptr_t)2;
    return pdPASS;
}
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
static inline int xQueueReceive(QueueHandle_t queue, void *event, TickType_t timeout)
{ (void)queue; (void)event; (void)timeout; return 0; }

#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define SPICOMMON_BUSFLAG_MASTER 1
#define SPI_CLK_SRC_SPLL 0
typedef struct {
    unsigned int length;
    const void *tx_buffer;
    void *rx_buffer;
    void *user;
} spi_transaction_t;
typedef void *spi_device_handle_t;
typedef struct {
    int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num;
    int data4_io_num, data5_io_num, data6_io_num, data7_io_num;
    unsigned int max_transfer_sz, flags;
} spi_bus_config_t;
typedef struct {
    int mode, clock_source, duty_cycle_pos, cs_ena_pretrans, cs_ena_posttrans;
    int clock_speed_hz, spics_io_num, queue_size;
    void (*pre_cb)(spi_transaction_t *);
    void (*post_cb)(spi_transaction_t *);
} spi_device_interface_config_t;
typedef struct test_dma_descriptor {
    void *buffer;
    struct { unsigned int length, size, suc_eof; } dw0;
    struct test_dma_descriptor *next;
} spi_dma_desc_t;
typedef struct { spi_dma_desc_t *dmadesc_tx; } spi_dma_ctx_t;
typedef struct {
    uint32_t val, ck_out_edge, doutdin, usr_command, usr_addr;
    uint32_t usr_dummy, usr_mosi, usr_miso, cs_setup, cs_hold;
} spi_user_reg_t;
typedef struct { uint32_t val, ck_idle_edge, cs0_dis, cs_keep_active; } spi_misc_reg_t;
typedef struct {
    spi_user_reg_t user;
    spi_misc_reg_t misc;
    struct { unsigned int ms_data_bitlen; } ms_dlen;
    struct { unsigned int wr_bit_order; } ctrl;
} spi_dev_t;
extern spi_dev_t test_spi;
#define SPI_LL_GET_HW(host) (&test_spi)
#define ADDR_DMA_2_CPU(address) (address)
#define CACHE_LL_L2MEM_NON_CACHE_ADDR(address) (address)
static inline bool esp_ptr_in_dram(const void *address) { return address != NULL; }
static inline spi_dma_ctx_t *spi_bus_get_dma_ctx(int host) { (void)host; return NULL; }
static inline int spi_bus_initialize(int host, const spi_bus_config_t *config, int dma)
{ (void)host; (void)config; (void)dma; return ESP_OK; }
static inline int spi_bus_add_device(int host, const spi_device_interface_config_t *config,
                                     spi_device_handle_t *handle)
{
    (void)host;
    assert(config->spics_io_num == 42 && config->cs_ena_pretrans == 1 &&
           config->cs_ena_posttrans == 1 && config->clock_speed_hz == 40000000);
    assert(config->pre_cb == NULL && config->post_cb == NULL);
    *handle = (void *)(uintptr_t)3;
    return ESP_OK;
}
static inline int spi_device_get_actual_freq(spi_device_handle_t handle, int *frequency)
{ (void)handle; *frequency = 40000; return ESP_OK; }
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
int test_spi_transmit(spi_transaction_t *transaction);
#endif
static inline int spi_device_polling_transmit(spi_device_handle_t handle,
                                             spi_transaction_t *transaction)
{
    (void)handle;
#ifdef CONFIG_ROBUSTO_HALOW_SPI_TX_TRACE
    return test_spi_transmit(transaction);
#else
    (void)transaction; assert(false); return ESP_FAIL;
#endif
}
static inline int spi_bus_remove_device(spi_device_handle_t handle)
{
    (void)handle;
    if (test_cleanup_failures != 0) { --test_cleanup_failures; return ESP_FAIL; }
    return ESP_OK;
}
static inline int spi_bus_free(int host) { (void)host; return ESP_OK; }

#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) < 55 && !test_invalid_gpio)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) GPIO_IS_VALID_GPIO(pin)
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
typedef struct { struct { uint32_t val; } out, out1; } test_gpio_t;
extern test_gpio_t test_gpio;
#define GPIO_LL_GET_HW(port) (&test_gpio)
static inline int gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
static inline int gpio_reset_pin(int pin) { (void)pin; return ESP_OK; }
static inline int gpio_get_level(int pin) { (void)pin; return 0; }
static inline int64_t esp_timer_get_time(void) { return (int64_t)test_ticks * 1000; }
uint32_t esp_random(void);

#define UART_NUM_2 2
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define UART_FIFO_OVF 1
#define UART_BUFFER_FULL 2
#define UART_PARITY_ERR 3
#define UART_FRAME_ERR 4
#define UART_BREAK 5
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
typedef struct { int type; } uart_event_t;
static inline bool uart_is_driver_installed(int port)
{ (void)port; ++test_uart_calls; return true; }
static inline int uart_param_config(int port, const uart_config_t *config)
{ (void)port; (void)config; ++test_uart_calls; return ESP_FAIL; }
static inline int uart_set_pin(int port, int tx, int rx, int rts, int cts)
{ (void)port; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_FAIL; }
static inline int uart_driver_install(int port, int rx, int tx, int events,
                                      QueueHandle_t *queue, int flags)
{ (void)port; (void)rx; (void)tx; (void)events; (void)queue; (void)flags; return ESP_FAIL; }
static inline int uart_read_bytes(int port, void *buffer, unsigned int count, int timeout)
{ (void)port; (void)buffer; (void)count; (void)timeout; return 0; }
static inline int uart_driver_delete(int port) { (void)port; return ESP_OK; }
