#include "canbus_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#ifdef USE_ESPIDF

#include <robusto_retval.h>
#include <robusto_qos.h>

#include <driver/gpio.h>
#include <driver/twai.h>
#include <string.h>

static char * canbus_messaging_log_prefix;
// TODO: Add in-flight message handling
// TODO: If a message is larger than X, free and fail that message

typedef struct in_flight {
    uint8_t address;
    uint16_t count; 
    uint8_t * data;
    bool available;
} in_flight_t;

#define CANBUS_MAX_IN_FLIGHT 5

in_flight_t in_flights[CANBUS_MAX_IN_FLIGHT];

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

/**
 * @brief Start a new in-flight
 * 
 * @param address The CAN bus address
 * @param count The current position
 * @param data The data to save
 * @param data_length The length of the data
 * @return int If positive, the inflight, if negative, if was the last, if -0xFFFFFF, count out-of-bounds
 */
int start_in_flight(uint8_t address, uint16_t count, uint8_t * data, uint8_t data_length) {
    // Find a free in-flight
    for (uint8_t curr_inflight = 0; curr_inflight < CANBUS_MAX_IN_FLIGHT; curr_inflight++) {
        if (in_flights->available) {
            in_flights[curr_inflight].address = address;
            if (count > CANBUS_MAX_PACKETS ) {
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Internal error, too many packages indicated.");
                return -2;
            }

            in_flights[curr_inflight].count = count; 
            in_flights[curr_inflight].data = robusto_malloc(count * TWAI_FRAME_MAX_DLC);
            if (!in_flights[curr_inflight].data) {
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Failed to allocate %i bytes for CAN bus in-flight.", count * TWAI_FRAME_MAX_DLC);
                return -3;
            }
            memcpy(in_flights[curr_inflight].data, data, data_length);
            return curr_inflight;
        }

        
    }
    ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Failed to find a free in_flight for bytes for CAN bus transmission from address %hu.", address);
    return -1;
}
/**
 * @brief Add data to an in-flight
 * 
 * @param address The CAN bus address
 * @param position The current position
 * @param data The data to save
 * @param data_length The length of the data
 * @return int If positive, the inflight, if negative, if was the last. Also -0xFFFFFF = count out-of-bounds, -0xFFFFFE = Invalid address
 */
int add_to_in_flight(uint8_t address, uint16_t position, uint8_t * data, uint8_t data_length) {
    // Find a free in-flight
    for (uint8_t curr_inflight = 0; curr_inflight < CANBUS_MAX_IN_FLIGHT; curr_inflight++) {
        if (in_flights[curr_inflight].address == address) {
            memcpy(in_flights[curr_inflight].data + (position * TWAI_FRAME_MAX_DLC), data, data_length);
            if (position == in_flights[curr_inflight].count -1) {
                return -curr_inflight;
            } else
            if (position > in_flights[curr_inflight].count -1) {
                return -0xFFFFFF;
            }
            else {
                return curr_inflight;
            }
            
        }
    }
    return -0xFFFFFE; 
    
}

/*     ------------------------------              Outgoing           ----------------------------                                       ------------------------------------------*/


rob_ret_val_t canbus_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt){
    
    // Stats: The Robusto CAN bus implementation can send up to 16 384 bytes in one message(beyond that, fragmentation must be used).
    // CAN has a 29-bit arbitration/addressing field. Robusto uses this in the following way:   
    // 1 bit, that if set indicates if it is the first packet of a transmission
    // 1 bit is reserved, will probably be used to help use the arbitration field to send an extra byte in one packet, which might be quite useful.
    // 11 bits will hold the number of packets of the transmission if it is the first, and the index of the packet if it is not.
    // 8 bits will hold the destination address byte (we will filter on these bits)
    // 8 bits will hold the source address byte

    // As the first bits are more significant for arbitration this means that the first packets alway gets prioritized, which improves throughput with an emphasis on shorter messages.
    // And then, among multipart messages, long messages, or messages that already has gotten far will get prioritized 

    // TOOD: There is a possibility to cram one more byte into the address field, which would make it possible to have 9 bytes of data in one packet.
    // This might be significant for very fast updates as that would enable the context byte + 2 uint32s in one frame, context byte + or a 16 bit serviceid + uint16_t an uint32_t. 
    // If we don't want this, we'll just use that bit to double the message length

    // BTW if you haven't, go watch "Nanook of the North" (Flaherty). 
    // It is free and a completely bonkers silent dramatized documentary about the inuit from 1922. 
    // If you think you're tough, it will take you out of that misconception. The inuit were the shit.
    
    uint32_t bytes_to_send = data_length - ROBUSTO_CRC_LENGTH;

    uint16_t number_of_packets = (bytes_to_send + TWAI_FRAME_MAX_DLC - 1) / TWAI_FRAME_MAX_DLC;
    if (number_of_packets > CANBUS_MAX_PACKETS) {
        ROB_LOGE(canbus_messaging_log_prefix, "Bug: The message is longer (%lu bytes) than the max length of the Robusto CAN bus implementation (%i bytes)", bytes_to_send, CANBUS_MAX_PACKETS * TWAI_FRAME_MAX_DLC);
        set_state(peer, &peer->canbus_info, robusto_mt_canbus, media_state_problem, media_problem_bug);
        return ROB_FAIL;
    }
    ROB_LOGW(canbus_messaging_log_prefix, "number_of_packets %" PRIx16 ", %u): ", number_of_packets,number_of_packets);
    
    twai_message_t message;
    message.identifier = 0;
    message.extd = 1;    
    message.identifier |= number_of_packets << 16;
    message.identifier |= get_host_peer()->canbus_address << 8;
    message.identifier |= peer->canbus_address;
    message.identifier |= 1 << 28; // Set bit 29, first message
    message.identifier &= ~(1 << 27); // Unset bit 28, reserved

//    message.identifier = 0xBCDDEEFF;
/*

message.identifier = 0xBCDDEEFF;
E (66063) UNNAMEDPEER: Identifier (1cddeeff): 
W (66063) UNNAMEDPEER: 255(xff) 238(xee) 221(xdd) 28(x1c)  
W (66067) UNNAMEDPEER: 11111111 01110111 10111011 00111000 
1100 1101

message.identifier = 0xCCDDEEFF;
W (228823) UNNAMEDPEER: 255(xff) 238(xee) 221(xdd) 12(x0c)  
W (228827) UNNAMEDPEER: 11111111 01110111 10111011 00110000 

message.identifier = 0xAAAAAAAA;
E (186923) UNNAMEDPEER: Identifier (aaaaaaa): 
W (186924) UNNAMEDPEER: 170(xaa) 170(xaa) 170(xaa) 10(x0a)  
W (186927) UNNAMEDPEER: 01010101 01010101 01010101 01010000 
message.identifier = 0xFFFFFFFF;
E (111943) UNNAMEDPEER: Identifier (1fffffff): 
W (111943) UNNAMEDPEER: 255(xff) 255(xff) 255(xff) 31(x1f)  
W (111947) UNNAMEDPEER: 11111111 11111111 11111111 11111000 

*/
    uint32_t bytes_sent = 0;
    while (bytes_to_send - bytes_sent > TWAI_FRAME_MAX_DLC) {
        memcpy(&message.data, data + bytes_sent, TWAI_FRAME_MAX_DLC);
        message.data_length_code = TWAI_FRAME_MAX_DLC;


        esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
        if (tr_result == ESP_OK) {
            ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
        } else {
            ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
            return ROB_FAIL;
        }                
        message.identifier &= ~(1 << 28); // Unset bit 29. Not the first packet from now on. Faster to do than check, probably.
        bytes_sent += TWAI_FRAME_MAX_DLC;
    }

    memcpy(&message.data , data + bytes_sent, bytes_to_send - bytes_sent);
    message.data_length_code = bytes_to_send - bytes_sent;
    esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (tr_result == ESP_OK) {
        ROB_LOGI(canbus_messaging_log_prefix, "Message queued for transmission");
    } else {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
        return ROB_FAIL;
    }

    // TODO: Explain why we use crc32 relation for wireless but not here for wired. (there are no others on wires)

    // This means that a Robusto CAN bus message always have two parts. 

    return ROB_OK;  

};

/*     ------------------------------              Incoming           ----------------------------                                       ------------------------------------------*/

int canbus_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes){

    // Treat all data lengths shorter than 8 bytes as single messages. Heart beats would be one variant.
    twai_message_t message;
    esp_err_t rec_result = twai_receive(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (rec_result == ESP_ERR_TIMEOUT) {
        ROB_LOGI(canbus_messaging_log_prefix, "Timed out waiting");
        return ROB_FAIL;
    } else
    if (rec_result != ESP_OK) {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to receive message, result code: %i", rec_result);
        return ROB_FAIL;
    } 


    ROB_LOGE(canbus_messaging_log_prefix, "Identifier (%" PRIx32 "): ", message.identifier);
    rob_log_bit_mesh(ROB_LOG_WARN, canbus_messaging_log_prefix, &message.identifier, 4);
    ROB_LOGE(canbus_messaging_log_prefix, "Data (length: %hu): ", message.data_length_code);
    rob_log_bit_mesh(ROB_LOG_WARN, canbus_messaging_log_prefix, &message.data, message.data_length_code);
    
    #if 0
    // Is it for us? The forth byte is our address
    if (((uint8_t*)&message.identifier)[3] == get_host_peer()->canbus_address) {
        // Is it the first packet of many?
        if (message.identifier & 1 << 4) {
            uint32_t sdfs = message.identifier & 
        }



        *peer = robusto_peers_find_peer_by_canbus_address((uint8_t*)&message.identifier)[2]);
        if (*peer == NULL) {
            if (message.data[0] & MSG_NETWORK) {
                // It is likely a presentation, add peer and hope it is. TODO: This could be flooded. 
                *peer = robusto_add_init_new_peer_canbus(NULL, data[0]);
            } else {
                // In I2C, which is a wired connection, we actually trust the peer so much that we 
                // Respond with a receipt that asks who it is, this is not done in wireless environments.
                i2c_send_receipt(NULL, false, true);
                robusto_free(data);
                return ROB_ERR_WHO;
            }
            
        }

        ROB_LOGD(i2c_esp32_messaging_log_prefix, "Message");
        rob_log_bit_mesh(ROB_LOG_DEBUG, i2c_esp32_messaging_log_prefix, data, data_len);
        // TODO: Move this to a more central spot and re-use. 
        is_ok = robusto_check_message(data,data_len, 1);
        is_heartbeat = data[I2C_ADDR_LEN + ROBUSTO_CRC_LENGTH] == HEARTBEAT_CONTEXT;
        if (is_heartbeat && is_ok) {  
            ROB_LOGI(i2c_esp32_messaging_log_prefix, "I2C is heartbeat");
            (*peer)->i2c_info.last_peer_receive = parse_heartbeat(data, I2C_ADDR_LEN + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
            
        } else if (i2c_send_receipt(*peer, is_ok, false) != ROB_OK) {
            ROB_LOGE(i2c_esp32_messaging_log_prefix, "failed sending receipt.");
        }
        
    }

    if (message.data_length_code < TWAI_FRAME_MAX_DLC) {
        // It is a short single message, possibly a heart beat.
        bool is_heartbeat = message.data[1] == HEARTBEAT_CONTEXT;
        if (is_heartbeat)
        {
            ROB_LOGI(canbus_messaging_log_prefix, "CAN bus is heartbeat");
            rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, &message.data + 1, message.data_length_code - 1);
            (*peer)->canbus_info.last_peer_receive = parse_heartbeat(&message.data + 1, ROBUSTO_CONTEXT_BYTE_LEN);
        }
    }
    // We have more data than what fits into a frame
    #endif
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