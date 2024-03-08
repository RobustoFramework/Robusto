#include "canbus_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#ifdef USE_ESPIDF
#include <robusto_retval.h>
#include <driver/gpio.h>
#include <driver/twai.h>

static char * canbus_messaging_log_prefix;

#if CONFIG_XTAL_FREQ == 26   // TWAI_CLK_SRC_XTAL = 26M
#define TWAI_TIMING_CONFIG_25KBITS()    {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((51 + 1) * (12 + 7 + 1)), .brp = 51, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_50KBITS()    {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((25 + 1) * (12 + 7 + 1)), .brp = 25, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_125KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((12 + 1) * (8 + 7 + 1)), .brp = 12, .tseg_1 = 8, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_250KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((7 + 1) * (5 + 7 + 1)), .brp = 7, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_500KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((3 + 1) * (5 + 7 + 1)), .brp = 3, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_800KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((2 + 1) * (3 + 7 + 1)), .brp = 2, .tseg_1 = 3, .tseg_2 = 7, .sjw = 3, .triple_sampling = false} // Note: Adjusted to nearest possible rate
#define TWAI_TIMING_CONFIG_1MBITS()     {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((1 + 1) * (5 + 7 + 1)), .brp = 1, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#endif  // CONFIG_XTAL_FREQ
// {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 247619 , .brp = 4, .tseg_1 = 13, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
rob_ret_val_t canbus_send_message(robusto_peer_t *peer, uint8_t *data, int data_length, bool receipt){
    //Configure message to transmit
    twai_message_t message;
    message.identifier = CONFIG_ROBUSTO_CANBUS_ADDRESS;
    message.extd = 1;
    message.data_length_code = 4;
    for (int i = 0; i < 4; i++) {
        message.data[i] = i;
    }
    esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(1000));
    if (tr_result == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
        return ROB_OK;
    } else {
        ROB_LOGI(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
        return ROB_FAIL;
    }
};

int canbus_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes){
    r_delay(1000);
    return canbus_send_message(NULL, NULL, 0, false);
};

rob_ret_val_t canbus_read_receipt(robusto_peer_t *peer){
    return ROB_OK;
};

rob_ret_val_t canbus_send_receipt(robusto_peer_t *peer, bool success, bool unknown){
    return ROB_OK;
};

int canbus_heartbeat(robusto_peer_t *peer){
    return 0;
};

void canbus_compat_messaging_start(void){
    //Start TWAI driver
    esp_err_t ret_start = twai_start(); 
    if (ret_start == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "CANbus TWAI Driver started.");
    } else {
        ROB_LOGI(canbus_messaging_log_prefix, "CANbus TWAI Failed to start driver. Code %i", ret_start);
        return;
    }
};

void canbus_compat_messaging_init(char * _log_prefix){
    canbus_messaging_log_prefix = _log_prefix;
    //Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_ROBUSTO_CANBUS_TX_IO, CONFIG_ROBUSTO_CANBUS_RX_IO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = {
    .clk_src = TWAI_CLK_SRC_DEFAULT,
    .brp = 4,
    .tseg_1 = 13,
    .tseg_2 = 7,
    .sjw = 1,
    .triple_sampling = false
};
    
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    //Install TWAI driver
    ROB_LOGI(canbus_messaging_log_prefix, "CAN bus installing.");
    esp_err_t ret_install = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret_install == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver installed.");
    } else {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Failed to install driver. Code: %i", ret_install);
        return;
    }
};

#endif
#endif