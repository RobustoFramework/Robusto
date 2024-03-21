#include "canbus_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#ifdef USE_ESPIDF

#include <robusto_retval.h>
#include <robusto_qos.h>
#include <robusto_incoming.h>

#include <driver/gpio.h>
#include <driver/twai.h>
#include <string.h>

static char *canbus_messaging_log_prefix;
// TODO: Add in-flight message handling
// TODO: If a message is larger than X, free and fail that message

typedef struct in_flight
{
    /* The source address of the sender*/
    uint8_t address;
    /* The number of frames*/
    uint16_t count;
    /* The data received*/
    uint8_t *data;
    /* Final length of data, will be 0 until last transmission done*/
    uint32_t data_length;
    /* Last updated position*/
    uint16_t last_package_index;
    /* This in-flight is taken */
    bool taken;
} in_flight_t;

#define CANBUS_ADDR_LEN 1
#define CANBUS_MAX_IN_FLIGHT 5
#define ERR_INVALID_PACKAGE_INDEX -0xFFFFFF
#define ERR_INFLIGHT_NOT_INITIALIZED -0xFFFFFE
#define ERR_INFLIGHT_NONE_AVAILABLE -1
#define ERR_INFLIGHT_TOO_MANY_PACKAGES -2
#define ERR_INFLIGHT_ALLOCATION_FAILED -3

in_flight_t in_flights[CANBUS_MAX_IN_FLIGHT];

// The following defines are test values awaiting ESP-IDF response to https://github.com/espressif/esp-idf/issues/13332 .
#if CONFIG_XTAL_FREQ == 26 // TWAI_CLK_SRC_XTAL = 26M
#warning "CAN bus for XTAL frequencies aren't properly supported by ESP-IDF for TWAI"
#define TWAI_TIMING_CONFIG_25KBITS()                                                                                                                                            \
    {                                                                                                                                                                           \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((51 + 1) * (12 + 7 + 1)), .brp = 51, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    }
#define TWAI_TIMING_CONFIG_50KBITS()                                                                                                                                            \
    {                                                                                                                                                                           \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((25 + 1) * (12 + 7 + 1)), .brp = 25, .tseg_1 = 12, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    }
#define TWAI_TIMING_CONFIG_125KBITS()                                                                                                                                         \
    {                                                                                                                                                                         \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((12 + 1) * (8 + 7 + 1)), .brp = 12, .tseg_1 = 8, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    }
#define TWAI_TIMING_CONFIG_250KBITS()                                                                                                             \
    {                                                                                                                                             \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 2000000, .brp = 0, .tseg_1 = 11, .tseg_2 = 4, .sjw = 3, .triple_sampling = false \
    }

// #define TWAI_TIMING_CONFIG_250KBITS()   {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((7 + 1) * (5 + 7 + 1)), .brp = 7, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}
#define TWAI_TIMING_CONFIG_500KBITS()                                                                                                                                       \
    {                                                                                                                                                                       \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((3 + 1) * (5 + 7 + 1)), .brp = 3, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    }
#define TWAI_TIMING_CONFIG_800KBITS()                                                                                                                                       \
    {                                                                                                                                                                       \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((2 + 1) * (3 + 7 + 1)), .brp = 2, .tseg_1 = 3, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    } // Note: Adjusted to nearest possible rate
#define TWAI_TIMING_CONFIG_1MBITS()                                                                                                                                         \
    {                                                                                                                                                                       \
        .clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 26000000 / ((1 + 1) * (5 + 7 + 1)), .brp = 1, .tseg_1 = 5, .tseg_2 = 7, .sjw = 3, .triple_sampling = false \
    }
#endif // CONFIG_XTAL_FREQ
// {.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 247619 , .brp = 4, .tseg_1 = 13, .tseg_2 = 7, .sjw = 3, .triple_sampling = false}

/**
 * @brief Start a new in-flight
 *
 * @param address The CAN bus address
 * @param count The current package_index
 * @param data The data to save
 * @param data_length The length of the data
 * @return int If positive, the inflight, if negative, if was the last, if -0xFFFFFF, count out-of-bounds
 */
int start_in_flight(uint8_t address, uint16_t count, uint8_t *data, uint8_t data_length)
{
    // Find a free in-flight
    for (uint8_t curr_inflight = 0; curr_inflight < CANBUS_MAX_IN_FLIGHT; curr_inflight++)
    {
        if (!in_flights[curr_inflight].taken)
        {
            in_flights[curr_inflight].taken = true;
            in_flights[curr_inflight].last_package_index = 0;
            in_flights[curr_inflight].address = address;
            if (count > CANBUS_MAX_PACKETS)
            {
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Internal error, too many packages indicated.");
                return ERR_INFLIGHT_TOO_MANY_PACKAGES;
            }

            in_flights[curr_inflight].count = count;
            in_flights[curr_inflight].data = robusto_malloc(count * TWAI_FRAME_MAX_DLC);
            if (!in_flights[curr_inflight].data)
            {
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Failed to allocate %i bytes for CAN bus in-flight.", count * TWAI_FRAME_MAX_DLC);
                return ERR_INFLIGHT_ALLOCATION_FAILED;
            }
            memcpy(in_flights[curr_inflight].data, data, data_length);
            return curr_inflight;
        }
    }
    ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Failed to find a free in_flight for bytes for CAN bus transmission from address %hu.", address);
    return ERR_INFLIGHT_NONE_AVAILABLE;
}
/**
 * @brief Add data to an in-flight
 *
 * @param address The CAN bus address
 * @param package_index The current package_index
 * @param data The data to save
 * @param data_length The length of the data
 * @return int If positive, the inflight, if negative, if was the last. Also -0xFFFFFF = count out-of-bounds, -0xFFFFFE = Invalid address
 */
int add_to_in_flight(uint8_t address, uint16_t package_index, uint8_t *data, uint8_t data_length)
{
    // Find a free in-flight
    for (uint8_t curr_inflight = 0; curr_inflight < CANBUS_MAX_IN_FLIGHT; curr_inflight++)
    {
        if (in_flights[curr_inflight].address == address)
        {
            if (package_index != in_flights[curr_inflight].last_package_index + 1)
            {
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Package index from %hu is not the next one. Was %u, should have been %u.",
                         address, package_index, in_flights[curr_inflight].last_package_index + 1);
                return ERR_INVALID_PACKAGE_INDEX;
            }
            in_flights[curr_inflight].last_package_index = package_index;
            memcpy(in_flights[curr_inflight].data + (package_index * TWAI_FRAME_MAX_DLC), data, data_length);
            if (package_index == in_flights[curr_inflight].count - 1)
            {
                ROB_LOGW(canbus_messaging_log_prefix, "CANBUS: We are at the last in flight from %hu.", address);
                in_flights[curr_inflight].data_length = (package_index * TWAI_FRAME_MAX_DLC) + data_length;
                return -curr_inflight;
            }
            else
            {
                return (package_index * TWAI_FRAME_MAX_DLC) + data_length;
            }
        }
    }
    return ERR_INFLIGHT_NOT_INITIALIZED;
}

/*     ------------------------------              Outgoing           ----------------------------                                       ------------------------------------------*/

rob_ret_val_t canbus_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{

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
    // If we don't want this, we'll just use that bit to double the possible message length

    // BTW if you haven't, go watch "Nanook of the North" (Flaherty).
    // It is free and a completely bonkers silent dramatized documentary about the inuit from 1922.
    // If you think you're tough, it will take you out of that misconception. The inuit were teh shit.

    // Offset Robusto preamble buffer (note that we don't add a byte for the address, this is done in the sending later)
    uint8_t *offset_data = data + ROBUSTO_CRC_LENGTH + ROBUSTO_PREFIX_BYTES;
    uint32_t offset_data_length = data_length - ROBUSTO_PREFIX_BYTES - ROBUSTO_CRC_LENGTH;

    uint16_t number_of_packets = (offset_data_length + TWAI_FRAME_MAX_DLC - 1) / TWAI_FRAME_MAX_DLC;
    if (number_of_packets > CANBUS_MAX_PACKETS)
    {
        ROB_LOGE(canbus_messaging_log_prefix, "Bug: The message is longer (%lu bytes) than the max length of the Robusto CAN bus implementation (%i bytes)", offset_data_length, CANBUS_MAX_PACKETS * TWAI_FRAME_MAX_DLC);
        set_state(peer, &peer->canbus_info, robusto_mt_canbus, media_state_problem, media_problem_bug);
        return ROB_FAIL;
    }
    ROB_LOGD(canbus_messaging_log_prefix, "Sending: number_of_packets %" PRIx16 "): ", number_of_packets);

    ROB_LOGD(canbus_messaging_log_prefix, "Data that will be sent (CRC32 and adressing will be excluded) actually sending: %lu bytes): ", offset_data_length);
    rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, offset_data, offset_data_length);

    twai_message_t message;
    message.extd = 1;             // Extended frame format
    message.rtr = 0;              // Not a remote transmission request
    message.ss = 0;               // Not a single-shot, will retry
    message.self = 0,             // Not a self reception request
        message.dlc_non_comp = 0, // DLC is not more than 8

        message.identifier = 0;
    message.identifier |= number_of_packets << 16;
    message.identifier |= get_host_peer()->canbus_address << 8;
    message.identifier |= peer->canbus_address;
    message.identifier |= 1 << 28;    // Set bit 29, first message
    message.identifier &= ~(1 << 27); // Unset bit 28, reserved

    uint32_t bytes_sent = 0;
    uint32_t package_index = 0;
    while (offset_data_length - bytes_sent > TWAI_FRAME_MAX_DLC)
    {
        memcpy(&message.data, offset_data + bytes_sent, TWAI_FRAME_MAX_DLC);
        message.data_length_code = TWAI_FRAME_MAX_DLC;
        ROB_LOGD(canbus_messaging_log_prefix, "Sending packet (length: %hu): ", message.data_length_code);
        rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, (uint8_t *)&(message.data), message.data_length_code);
        esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
        if (tr_result == ESP_OK)
        {
            ROB_LOGD(canbus_messaging_log_prefix, "Message queued for transmission");
            add_to_history(&peer->canbus_info, true, tr_result);
        }
        else
        {
            ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
            return ROB_FAIL;
        }
        package_index++;
        message.identifier = 0;
        message.identifier |= package_index << 16;
        message.identifier |= get_host_peer()->canbus_address << 8;
        message.identifier |= peer->canbus_address;
        message.identifier &= ~(1 << 28); // Unset bit 29. Not the first packet from now on. Faster to do than check, probably.
        bytes_sent += TWAI_FRAME_MAX_DLC;
    }

    memcpy(&message.data, offset_data + bytes_sent, offset_data_length - bytes_sent);
    message.data_length_code = offset_data_length - bytes_sent;
    ROB_LOGD(canbus_messaging_log_prefix, "Sending packet (length: %hu): ", message.data_length_code);
    rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, (uint8_t *)&(message.data), message.data_length_code);
    esp_err_t tr_result = twai_transmit(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (tr_result == ESP_OK)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "Message queued for transmission");
    }
    else
    {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message for transmission, error_code: %i", tr_result);
        return ROB_FAIL;
    }

    // TODO: Explain why we use crc32 relation for wireless but not here for wired. (there are no others on wires)

    // This means that a Robusto CAN bus message always have two parts.

    return ROB_OK;
};

/*     ------------------------------              Incoming           ----------------------------                                       ------------------------------------------*/

int canbus_read_data(uint8_t **rcv_data, robusto_peer_t **peer, uint8_t *prefix_bytes)
{

    // TODO: Treat all data lengths shorter than 8 bytes as single messages. Heart beats would be one variant.
    // TODO: Stop sending and receiving CRC (add parameter to ), it is included in CAN bus and
    twai_message_t message;
    esp_err_t rec_result = twai_receive(&message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS));
    if (rec_result == ESP_ERR_TIMEOUT)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "Timed out waiting");
        return ROB_FAIL;
    }
    else if (rec_result != ESP_OK)
    {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to receive message, result code: %i", rec_result);
        return ROB_FAIL;
    }
    uint8_t dest = (uint8_t)(message.identifier);
    if (dest != get_host_peer()->canbus_address)
    {
        return ROB_OK;
    }

    uint8_t source = (uint8_t)(message.identifier >> 8);
    /*
    ROB_LOGE(canbus_messaging_log_prefix, "Source: %hu, Dest: %hu", source, dest);
    ROB_LOGE(canbus_messaging_log_prefix, "Identifier (%" PRIx32 "): ", message.identifier);
    rob_log_bit_mesh(ROB_LOG_WARN, canbus_messaging_log_prefix, &message.identifier, 4);
    ROB_LOGE(canbus_messaging_log_prefix, "Data (length: %hu): ", message.data_length_code);
    rob_log_bit_mesh(ROB_LOG_WARN, canbus_messaging_log_prefix, message.data, message.data_length_code);
*/
    uint8_t *data = NULL;
    uint32_t data_length = 0;
    // How many packets are there? It is a 12-bit value, so move the data 16 bits to the right and zero out bit 12-16.
    uint16_t packet_count_index = (message.identifier >> 16) & ~(0xF << 12);
    ROB_LOGD(canbus_messaging_log_prefix, "Parsed packet count %u", packet_count_index);
    // It is the first package? Is the 29th bit set?
    bool first_packet = (message.identifier & 1 << 28);

    if (first_packet)
    {
        if (packet_count_index > 1)
        {
            // if there is more than one packet, add the data to a new in-flight and return
            int res_start = start_in_flight(source, packet_count_index, &message.data, message.data_length_code);
            if (res_start > 0)
            {
                return ROB_OK;
            }
            else
            {
                return ROB_FAIL;
            }
        }
        else
        {
            // if not, we move on.
            // NOTE: We might gain an extra byte by using the package index field if it is a single by using bit 28. Worth it?
            data_length = message.data_length_code;
            data = robusto_malloc(data_length);
            memcpy(data, &message.data, data_length);
        }
    }
    else
    {

        ROB_LOGD(canbus_messaging_log_prefix, "Parsed packet index %u", packet_count_index);

        int res_add = add_to_in_flight(source, packet_count_index, &message.data, message.data_length_code);
        if (res_add == ERR_INVALID_PACKAGE_INDEX)
        {
            return ROB_FAIL;
        }
        else if (res_add == ERR_INFLIGHT_NOT_INITIALIZED)
        {
            return ROB_FAIL;
        }
        else if (res_add <= 0) // Neither invalid or unitialized, then it is the reference to the in flight.
        {
            data = robusto_malloc(in_flights[-res_add].data_length);
            memcpy(data, in_flights[-res_add].data, in_flights[-res_add].data_length);

            robusto_free(in_flights[-res_add].data);
            in_flights[-res_add].data = NULL;
            data_length = in_flights[-res_add].data_length;
            in_flights[-res_add].data_length = 0;
            in_flights[-res_add].taken = false;
            ROB_LOGD(canbus_messaging_log_prefix, "Full data (length: %lu): ", data_length);
            rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, data, data_length);
            // We have a full message, continuings. Not that all other cases must return.
        }
        else
        {
            ROB_LOGD(canbus_messaging_log_prefix, "Data so far from %hu: %i bytes", source, res_add);
            return ROB_OK;
        }
    }

    *peer = robusto_peers_find_peer_by_canbus_address(source);
    if (*peer == NULL)
    {
        if (data[0] & MSG_NETWORK)
        {
            // It is likely a presentation, add peer and hope it is. TODO: This could be flooded.
            *peer = robusto_add_init_new_peer_canbus(NULL, source);
        }
        else
        {
            // In CAN bus, which is a wired connection, we actually trust the peer so much that we
            // Respond with a receipt that asks who it is, this is not done in wireless environments.
            canbus_send_receipt(NULL, false, true);
            robusto_free(data);
            return ROB_ERR_WHO;
        }
    }
    add_to_history(&((*peer)->canbus_info), false, ROB_OK);
    //ROB_LOGI(canbus_messaging_log_prefix, "Message");
    //rob_log_bit_mesh(ROB_LOG_INFO, canbus_messaging_log_prefix, data, data_length);
    // TODO: Move this to a more central spot and re-use.

    bool is_heartbeat = data[0] == HEARTBEAT_CONTEXT;
    if (is_heartbeat)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "CAN bus is heartbeat");
        (*peer)->canbus_info.last_peer_receive = parse_heartbeat(data, ROBUSTO_CONTEXT_BYTE_LEN);
    }
    (*peer)->canbus_info.last_receive = r_millis();

    if (is_heartbeat)
    {
        add_to_history(&((*peer)->canbus_info), false, ROB_OK);
        robusto_free(data);
        return ROB_OK;
    }
    else
    {
        // Pass on message, negative offset is because it has built-in CRC.
        add_to_history(&((*peer)->canbus_info), false, robusto_handle_incoming(data, data_length, *peer, robusto_mt_canbus, -ROBUSTO_CRC_LENGTH));
        return data_length;
    }

    return ROB_OK;
};

rob_ret_val_t canbus_read_receipt(robusto_peer_t *peer)
{
    return ROB_OK;
};

rob_ret_val_t canbus_send_receipt(robusto_peer_t *peer, bool success, bool unknown)
{
    return ROB_OK;
};

int canbus_heartbeat(robusto_peer_t *peer)
{
    return 0;
};

void canbus_compat_messaging_start(void)
{
    // Start TWAI driver
    esp_err_t ret_start = twai_start();
    if (ret_start == ESP_OK)
    {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver started.");
    }
    else
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to start driver. Code %i", ret_start);
        return;
    }
};

void canbus_compat_messaging_init(char *_log_prefix)
{
    canbus_messaging_log_prefix = _log_prefix;
    // Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_ROBUSTO_CANBUS_TX_IO, CONFIG_ROBUSTO_CANBUS_RX_IO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    ROB_LOGI(canbus_messaging_log_prefix, "CAN bus installing.");
    esp_err_t ret_install = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret_install == ESP_OK)
    {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver installed.");
    }
    else
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to install driver. Code: %i", ret_install);
        return;
    }
};

#endif
#endif