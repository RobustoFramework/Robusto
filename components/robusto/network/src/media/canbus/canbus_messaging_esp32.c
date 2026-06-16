#include "canbus_messaging.h"
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
#ifdef USE_ESPIDF

#include <robusto_retval.h>
#include <robusto_qos.h>
#include <robusto_incoming.h>

#include <esp_twai.h>
#include <esp_twai_onchip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <hal/gpio_types.h>
#include <string.h>
#include <robusto_time.h>

#define CANBUS_ADDR_LEN 1
#define CANBUS_MAX_IN_FLIGHT 5
#define ERR_INVALID_PACKAGE_INDEX -0xFFFFFF
#define ERR_INFLIGHT_NOT_INITIALIZED -0xFFFFFE
#define ERR_INFLIGHT_NONE_AVAILABLE -1
#define ERR_INFLIGHT_TOO_MANY_PACKAGES -2
#define ERR_INFLIGHT_ALLOCATION_FAILED -3

static char *canbus_messaging_log_prefix;

/* Last known status of the TWAI controller */
static twai_node_status_t status_info;
static twai_node_record_t record_info;
static twai_node_handle_t canbus_twai_node;
static QueueHandle_t canbus_rx_queue;
static bool canbus_twai_running;

typedef struct canbus_twai_frame
{
    uint32_t identifier;
    uint16_t data_length_code;
    uint8_t data[TWAI_FRAME_MAX_DLC];
} canbus_twai_frame_t;

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

void canbus_twai_install();

in_flight_t in_flights[CANBUS_MAX_IN_FLIGHT];

static uint32_t canbus_twai_bitrate(void)
{
#ifdef CONFIG_ROBUSTO_CANBUS_BIT_RATE_1MBITS
    return 1000000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_800KBITS)
    return 800000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_500KBITS)
    return 500000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_250KBITS)
    return 250000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_125KBITS)
    return 125000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_100KBITS)
    return 100000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_50KBITS)
    return 50000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_25KBITS)
    return 25000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_20KBITS)
    return 20000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_16KBITS)
    return 16000;
#elif defined(CONFIG_ROBUSTO_CANBUS_BIT_RATE_12_5KBITS)
    return 12500;
#else
    return 125000;
#endif
}

static bool canbus_twai_rx_done_cb(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    QueueHandle_t rx_queue = (QueueHandle_t)user_ctx;
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint8_t rx_data[TWAI_FRAME_MAX_DLC] = {0};
    twai_frame_t rx_frame = {0};
    rx_frame.buffer = rx_data;
    rx_frame.buffer_len = sizeof(rx_data);

    while (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK)
    {
        canbus_twai_frame_t queue_frame = {0};
        queue_frame.identifier = rx_frame.header.id;
        queue_frame.data_length_code = twaifd_dlc2len(rx_frame.header.dlc);
        if (queue_frame.data_length_code > TWAI_FRAME_MAX_DLC)
        {
            queue_frame.data_length_code = TWAI_FRAME_MAX_DLC;
        }
        memcpy(queue_frame.data, rx_data, queue_frame.data_length_code);
        if (rx_queue != NULL)
        {
            xQueueSendFromISR(rx_queue, &queue_frame, &higher_priority_task_woken);
        }
        memset(rx_data, 0, sizeof(rx_data));
        rx_frame.buffer = rx_data;
        rx_frame.buffer_len = sizeof(rx_data);
    }

    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t canbus_twai_transmit(uint32_t identifier, const uint8_t *data, uint16_t data_length)
{
    if (canbus_twai_node == NULL || !canbus_twai_running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx_data[TWAI_FRAME_MAX_DLC] = {0};
    if (data_length > TWAI_FRAME_MAX_DLC)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(tx_data, data, data_length);

    twai_frame_t frame = {0};
    frame.header.id = identifier;
    frame.header.ide = true;
    frame.header.rtr = false;
    frame.header.dlc = data_length;
    frame.buffer = tx_data;
    frame.buffer_len = data_length;

    esp_err_t transmit_result = twai_node_transmit(canbus_twai_node, &frame, CANBUS_TIMEOUT_MS);
    if (transmit_result == ESP_OK)
    {
        transmit_result = twai_node_transmit_wait_all_done(canbus_twai_node, CANBUS_TIMEOUT_MS);
    }
    return transmit_result;
}

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
                ROB_LOGE(canbus_messaging_log_prefix, "CANBUS: Package index from %hu is not the next one. Was %u, should have been %u, invalidating in-flight.",
                         address, package_index, in_flights[curr_inflight].last_package_index + 1);
                // We invalidate the package, this should never happen.
                in_flights[curr_inflight].taken = false;
                in_flights[curr_inflight].address = 0;
                
                return ERR_INVALID_PACKAGE_INDEX;
            }
            in_flights[curr_inflight].last_package_index = package_index;
            memcpy(in_flights[curr_inflight].data + (package_index * TWAI_FRAME_MAX_DLC), data, data_length);
            if (package_index == in_flights[curr_inflight].count - 1)
            {
                ROB_LOGI(canbus_messaging_log_prefix, "CANBUS: We are at the last in flight from %hu.", address);
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

void handle_twai_error(esp_err_t tr_result)
{
    if (canbus_twai_node != NULL)
    {
        twai_node_get_info(canbus_twai_node, &status_info, &record_info);
    }
    ROB_LOGI(canbus_messaging_log_prefix, "TWAI information: \nState: %u \n bus_error_count: %lu, rx_error_counter: %hu, tx_error_counter: %hu, tx_queue_remaining: %lu",
             status_info.state, record_info.bus_err_num, status_info.rx_error_count, status_info.tx_error_count, status_info.tx_queue_remaining);

    switch (tr_result)
    {
    case ESP_ERR_TIMEOUT:
        ROB_LOGW(canbus_messaging_log_prefix, "TWAI error 263: Timeout");
        break;
    case ESP_ERR_INVALID_STATE:
        ROB_LOGE(canbus_messaging_log_prefix, "TWAI error 259: Invalid State");
        if (status_info.tx_error_count > 127)
        {

            if (status_info.state != TWAI_ERROR_BUS_OFF)
            {
                ROB_LOGI(canbus_messaging_log_prefix, "TWAI: Bus wasn't off, will not initiate recovery.");
                break;
            }
            else
            {
                ROB_LOGI(canbus_messaging_log_prefix, "TWAI: Bus is off, initiating recovery.");
                esp_err_t recover_result = twai_node_recover(canbus_twai_node);
                if (recover_result != ESP_OK)
                {
                    ROB_LOGE(canbus_messaging_log_prefix, "TWAI: Failed to initiate recovery. Code: %i", recover_result);
                    break;
                }
                uint32_t start_time = r_millis();
                while (status_info.state == TWAI_ERROR_BUS_OFF && (r_millis() - start_time < 1000))
                {
                    r_delay(500);
                    twai_node_get_info(canbus_twai_node, &status_info, &record_info);
                }
            }
        }
        break;
    case ESP_ERR_INVALID_CRC:
        ROB_LOGW(canbus_messaging_log_prefix, "TWAI error 265: Invalid CRC");
        break;
    default:
        break;
    }
}

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

    if (canbus_twai_node == NULL || !canbus_twai_running)
    {
        if (canbus_twai_node != NULL)
        {
            twai_node_get_info(canbus_twai_node, &status_info, &record_info);
        }
        ROB_LOGE(canbus_messaging_log_prefix, "canbus_send_message: TWAI CAN bus is not running");
        return ROB_FAIL;
    }

    // BTW if you haven't, go watch "Nanook of the North" (Flaherty).
    // It is free and a completely bonkers silent dramatized documentary about the inuit from 1922.
    // If you think you're tough, it will take you out of that misconception. The inuit were teh shit.
    // Offset Robusto preamble buffer (note that we don't add a byte for the address, this is done in the sending later)
    uint8_t *offset_data = data + CANBUS_MESSAGE_OFFSET;
    uint32_t offset_data_length = data_length - CANBUS_MESSAGE_OFFSET;

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

    uint32_t identifier = 0;
    identifier |= number_of_packets << 16;
    identifier |= get_host_peer()->canbus_address << 8;
    identifier |= peer->canbus_address;
    identifier |= 1 << 28;    // Set bit 29, first message
    identifier &= ~(1 << 27); // Unset bit 28, reserved

    uint32_t bytes_sent = 0;
    uint32_t package_index = 0;
    while (offset_data_length - bytes_sent > TWAI_FRAME_MAX_DLC)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "Sending packet (length: %hu): ", TWAI_FRAME_MAX_DLC);
        rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, offset_data + bytes_sent, TWAI_FRAME_MAX_DLC);
        esp_err_t tr_result = canbus_twai_transmit(identifier, offset_data + bytes_sent, TWAI_FRAME_MAX_DLC);
        if (tr_result == ESP_OK)
        {
            ROB_LOGD(canbus_messaging_log_prefix, "Message queued for transmission");
            add_to_history(&peer->canbus_info, true, tr_result);
        }
        else
        {
            ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message to %hhu for transmission, error_code: 0x%x", peer->canbus_address, tr_result);
            handle_twai_error(tr_result);
            return ROB_FAIL;
        }
        package_index++;
        identifier = 0;
        identifier |= package_index << 16;
        identifier |= get_host_peer()->canbus_address << 8;
        identifier |= peer->canbus_address;
        identifier &= ~(1 << 28); // Unset bit 29. Not the first packet from now on. Faster to do than check, probably.
        bytes_sent += TWAI_FRAME_MAX_DLC;
    }

    uint16_t final_data_length = offset_data_length - bytes_sent;
    ROB_LOGD(canbus_messaging_log_prefix, "Sending packet (length: %hu): ", final_data_length);
    rob_log_bit_mesh(ROB_LOG_DEBUG, canbus_messaging_log_prefix, offset_data + bytes_sent, final_data_length);
    esp_err_t tr_result = canbus_twai_transmit(identifier, offset_data + bytes_sent, final_data_length);
    if (tr_result == ESP_OK)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "Message queued for transmission");
    }
    else
    {
        ROB_LOGW(canbus_messaging_log_prefix, "Failed to queue message to %hhu for transmission, error_code: 0x%x", peer->canbus_address, tr_result);
        handle_twai_error(tr_result);
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
    canbus_twai_frame_t message;
    if (canbus_rx_queue == NULL)
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus RX queue is not initialized.");
        return ROB_FAIL;
    }
    if (xQueueReceive(canbus_rx_queue, &message, pdMS_TO_TICKS(CANBUS_TIMEOUT_MS)) != pdTRUE)
    {
        ROB_LOGD(canbus_messaging_log_prefix, "Timed out waiting");
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
            int res_start = start_in_flight(source, packet_count_index, message.data, message.data_length_code);
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
            memcpy(data, message.data, data_length);
        }
    }
    else
    {

        ROB_LOGD(canbus_messaging_log_prefix, "Parsed packet index %u", packet_count_index);

        int res_add = add_to_in_flight(source, packet_count_index, message.data, message.data_length_code);
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
            // TODO: Implement "who"-message. It will significally speed up reconnections elliminating the client timeout.
            canbus_send_receipt(NULL, false, true);
            robusto_free(data);
            return ROB_ERR_WHO;
        }
    }
    add_to_history(&((*peer)->canbus_info), false, ROB_OK);
    // ROB_LOGI(canbus_messaging_log_prefix, "Message");
    // rob_log_bit_mesh(ROB_LOG_INFO, canbus_messaging_log_prefix, data, data_length);
    //  TODO: Move this to a more central spot and re-use.

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
    if (canbus_twai_node == NULL)
    {
        canbus_twai_install();
    }
    if (canbus_twai_node == NULL)
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI node is not installed.");
        return;
    }
    if (canbus_rx_queue != NULL)
    {
        xQueueReset(canbus_rx_queue);
    }
    esp_err_t ret_start = twai_node_enable(canbus_twai_node);
    if (ret_start == ESP_OK)
    {
        canbus_twai_running = true;
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver started.");
    }
    else
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to start driver. Code %i", ret_start);
        return;
    }
};

void canbus_twai_install()
{
    if (canbus_twai_node != NULL)
    {
        return;
    }
    if (canbus_rx_queue == NULL)
    {
        canbus_rx_queue = xQueueCreate(CANBUS_MAX_IN_FLIGHT * 2, sizeof(canbus_twai_frame_t));
        if (canbus_rx_queue == NULL)
        {
            ROB_LOGE(canbus_messaging_log_prefix, "CAN bus failed to allocate RX queue.");
            return;
        }
    }

    twai_onchip_node_config_t node_config = {0};
    node_config.io_cfg.tx = CONFIG_ROBUSTO_CANBUS_TX_IO;
    node_config.io_cfg.rx = CONFIG_ROBUSTO_CANBUS_RX_IO;
    node_config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    node_config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    node_config.bit_timing.bitrate = canbus_twai_bitrate();
    node_config.tx_queue_depth = CANBUS_MAX_IN_FLIGHT;
    node_config.fail_retry_cnt = -1;
    node_config.flags.no_receive_rtr = true;

    // Install TWAI driver
    ROB_LOGI(canbus_messaging_log_prefix, "CAN bus installing.");
    esp_err_t ret_install = twai_new_node_onchip(&node_config, &canbus_twai_node);
    if (ret_install == ESP_OK)
    {
        twai_event_callbacks_t callbacks = {0};
        callbacks.on_rx_done = canbus_twai_rx_done_cb;
        ret_install = twai_node_register_event_callbacks(canbus_twai_node, &callbacks, canbus_rx_queue);
    }
    if (ret_install == ESP_OK)
    {
        ROB_LOGI(canbus_messaging_log_prefix, "CAN bus TWAI Driver installed.");
    }
    else
    {
        ROB_LOGE(canbus_messaging_log_prefix, "CAN bus TWAI Failed to install driver. Code: %i", ret_install);
        return;
    }


}

void canbus_compat_messaging_init(char *_log_prefix)
{
    canbus_messaging_log_prefix = _log_prefix;

    canbus_twai_install();
};

#endif
#endif