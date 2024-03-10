#include "canbus_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#ifdef USE_ESPIDF

#include <robusto_retval.h>
#include <robusto_qos.h>

#include <driver/gpio.h>
#include <driver/twai.h>


static char * canbus_messaging_log_prefix;

#if CONFIG_XTAL_FREQ == 26   // TWAI_CLK_SRC_XTAL = 26M
#define TWAI_TIMING_CONFIG_25KBITS()    {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((51 + 1) * (12 + 7 + 1)), .brp = 51, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_50KBITS()    {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((25 + 1) * (12 + 7 + 1)), .brp = 25, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_125KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((12 + 1) * (8 + 7 + 1)), .brp = 12, .tseg_1 = 8, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_250KBITS()   { .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 2000000, .brp = 0, .tseg_1 = 11, .tseg_2 = 4,  .sjw = 3,  .triple_sampling = false}   

//#define TWAI_TIMING_CONFIG_250KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((7 + 1) * (5 + 7 + 1)), .brp = 7, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_500KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((3 + 1) * (5 + 7 + 1)), .brp = 3, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_800KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((2 + 1) * (3 + 7 + 1)), .brp = 2, .tseg_1 = 3, .tseg_2 = 7, .sjw = 3, .triple_sampling = false} // Note: Adjusted to nearest possible rate
#define TWAI_TIMING_CONFIG_1MBITS()     {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((1 + 1) * (5 + 7 + 1)), .brp = 1, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#endif  // CONFIG_XTAL_FREQ
// {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 247619 , .brp = 4, .tseg_1 = 13, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
rob_ret_val_t canbus_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt){
    
    // Stats: The Robusto CAN bus implementation can send up to 32 678 bytes in one message(beyond that, fragmentation must be used).
    // CAN has a 29-bit arbitration/addressing field. Robusto uses this in the following way:
    // 8 bits will hold the destination address byte (we will filter on these bits)
    // 8 bits will hold the source address byte
    // 1 bit, that if set indicates if it is the first packet of a transmission
    // 12 bits will hold the number of packets of the transmission if it is the first, and the index of the packet if it is not.
    uint32_t number_of_packets = (data_length + TWAI_FRAME_MAX_DLC - 1) / TWAI_FRAME_MAX_DLC;
    if (number_of_packets > CANBUS_MAX_PACKETS) {
        ROB_LOGI(canbus_messaging_log_prefix, "The message is longer (%lu bytes) than the max length of the Robusto CAN bus implementation (%lu bytes)", data_length, CANBUS_MAX_PACKETS * TWAI_FRAME_MAX_DLC);
    }
    twai_message_t message;
    message.identifier = number_of_packets;
    message.extd = 1;    
    get_host_peer()->canbus_address
    peer->canbus_address

    // TODO: Explain why we use crc32 relation for wireless but not here for wired. (there are no others on wires)
    
    // This means that a Robusto CAN bus message always have two parts. 

    
    // If message length minus the CRC it is less than the max framelength (usually 8 bytes), we send it as a single message
    if (data_length - ROBUSTO_CRC_LENGTH < TWAI_FRAME_MAX_DLC) {

        // TODO: CAN bus does CRC on each message, so we will not need to bring it it on messages that fit within a single frame, making a lot of messages fit within one frame.
        // We will bring the CRC with us on longer messages though.

        message.data_length_code = data_length;
        memcpy(&message.data, data + ROBUSTO_CRC_LENGTH, data_length);
        esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
        if (tr_result == ESP_OK) {
            ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
            return ROB_OK;
        } else {
            ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
            return ROB_FAIL;
        }           
    }
    // It is 8 or more bytes, we need to send multiple messages.
    uint32_t data_left = data_length;
    // The first will contain the length of the data and the first 4 bytes.
    twai_message_t curr_message;
    curr_message.identifier = peer->relation_id_outgoing;
    curr_message.extd = 1;
    curr_message.data_length_code = TWAI_FRAME_MAX_DLC;
    memcpy(&curr_message.data, data_length, sizeof(data_length));
    memcpy(&curr_message.data + sizeof(data_length), TWAI_FRAME_MAX_DLC - sizeof(data_length));
    data_left -= TWAI_FRAME_MAX_DLC - sizeof(data_length);
    esp_err_t tr_result = twai_transmit(&curr_message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (tr_result == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
        
    } else {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
        return ROB_FAIL;
    }     
    while (data_left > 0) {
        if (data_left > TWAI_FRAME_MAX_DLC) {
            curr_message.data_length_code = TWAI_FRAME_MAX_DLC;
            data_left -= 8;
        }
        else {
            curr_message.data_length_code = data_left;
        }
        // TODO: Add retries here? Or do we only do that on a higher level?

        memcpy(&curr_message.data, data, curr_message.data_length_code);
        esp_err_t tr_result = twai_transmit(&curr_message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
        if (tr_result == ESP_OK) {
            ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
        } else {
            ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
            return ROB_FAIL;
        }       
    } 
    return ROB_OK;  

};

int canbus_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes){

    // Treat all data lengths shorter than 8 bytes as single messages. Heart beats would be one variant.
    twai_message_t message;
    esp_err_t rec_result = twai_receive(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (rec_result != ESP_OK) {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to receive message, error_code: %i", rec_result);
        return ROB_FAIL;
    } 

    if (message.data_length_code = TWAI_FRAME_MAX_DLC) {
        // It is a short single message, possibly a heart beat.

        bool is_heartbeat = message.data[1] == HEARTBEAT_CONTEXT;
        if (is_heartbeat)
        {
            ROB_LOGI(canbus_messaging_log_prefix, "CAN bus is heartbeat");
            rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, &message.data + 1, message.data_length_code - 1);
            *peer->canbus_info.last_peer_receive = parse_heartbeat(&message.data + 1, ROBUSTO_CONTEXT_BYTE_LEN);
        }


    }

        // We have more data than what fits into a frame

    ROB_LOGI(canbus_messaging_log_prefix, "Message received");
    return ROB_OK;

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
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver started.");
    } else {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to start driver. Code %i", ret_start);
        return;
    }
};

void canbus_compat_messaging_init(char * _log_prefix){
    canbus_messaging_log_prefix = _log_prefix;
    //Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_ROBUSTO_CANBUS_TX_IO, CONFIG_ROBUSTO_CANBUS_RX_IO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    //Install TWAI driver
    ROB_LOGI(canbus_messaging_log_prefix, "CAN bus installing.");
    esp_err_t ret_install = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret_install == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver installed.");
    } else {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to install driver. Code: %i", ret_install);
        return;
    }
};

#endif
#endif