#include "robusto_message.h"
#include "robusto_logging.h"

#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#else
#include <sys/queue.h>
#endif

/* Used to identify fragmented message data (as opposed to the initiating message) */
#define FRAGMENTED_DATA_CONTEXT MSG_FRAGMENTED + 0x40

// Define a short cut. 4 the fragment counter uint32_t bytes.
#define FRAG_HEADER_LEN (ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN + 1 + 4)

// A callback that can be used to send messaged
typedef void cb_send_message(robusto_peer_t *peer, const uint8_t *data, int len, bool receipt);

// TODO: If this works, centralize fragmented handling for all medias (will a stream be similar?) This is the specific fragmented case
typedef struct fragmented_message
{
    // The hash of the message, used to identify the message
    uint32_t hash;
    // The data to send
    uint8_t *data;
    // The length of the message
    uint32_t data_length;
    // The number of fragments 
    uint32_t fragment_count;
    // Size of each fragment
    uint32_t fragment_size;
    // The latest (not the last) fragment
    uint32_t latest_fragment;
    // A map of all the fragments that has been received (not automatically updated on the sender)
    uint8_t *received_fragments; // TODO: This should instead be a bitmap to save space.
    /* When the frag_msg was created, a non-used element */
    uint32_t start_time;

    SLIST_ENTRY(fragmented_message) fragmented_messages; /* Singly linked list */

} fragmented_message_t;

SLIST_HEAD(fragmented_message_head_t, fragmented_message);

struct fragmented_message_head_t fragmented_message_head;

// Cache the last frag_msg for optimization
fragmented_message_t *last_frag_msg = NULL;

char *fragmentation_log_prefix = "NOT SET";
/**
 * @brief Handle a
 *
 * @param peer
 * @param data
 * @param len
 * @param fragment_size
 */
void handle_frag_request(robusto_peer_t *peer, const uint8_t *data, int len, uint32_t fragment_size)
{

    // Manually check CRC32 hash
    if (*(uint32_t *)(data) != robusto_crc32(0, data + 4, 17))
    {
        add_to_history(&peer->espnow_info, false, ROB_FAIL);
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because hash mismatch.");
        return;
    }

    fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
    frag_msg->data_length = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 1);
    frag_msg->fragment_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 5);
    frag_msg->fragment_size = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 9);
    frag_msg->hash = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 13);
    // TODO: How big should we allow before SPIRAM and more?
    frag_msg->data = robusto_malloc(frag_msg->data_length);
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);

    ROB_LOGI(fragmentation_log_prefix, "Fragmented initialization received, info:\n \
        data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
                frag_msg->data_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);

    /* When the frag_msg was created, a non-used element */
    frag_msg->start_time = r_millis();
    SLIST_INSERT_HEAD(&fragmented_message_head, frag_msg, fragmented_messages);
    last_frag_msg = frag_msg;
    peer->espnow_info.last_receive = r_millis();
}

void handle_frag_message(robusto_peer_t *peer, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message * send_message)
{
    // Initiate a new fragmented  (...stream?)
    fragmented_message_t *frag_msg;
    if ((last_frag_msg) && (last_frag_msg->hash != *(uint32_t *)data))
    {
        // Not the last fragmented message, find it
        ROB_LOGE(fragmentation_log_prefix, "Time to implement looking up the fragment message");
        return;
    }
    else
    {
        ROB_LOGI(fragmentation_log_prefix, "Matched with cached frag");
        frag_msg = last_frag_msg;
    }
    uint32_t msg_frag_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 1);

    // We check the length of all fragments, start with calculating what the current should be
    uint32_t curr_frag_size = frag_msg->fragment_size;
    // TOOD: Handle an excplicit 0xFFFFFFFF
    if (msg_frag_count == frag_msg->fragment_count - 1)
    {
        // If it is the last fragment, it is whatever is left
        curr_frag_size = frag_msg->data_length - (frag_msg->fragment_size * msg_frag_count);
    }
    uint32_t expected_message_length = FRAG_HEADER_LEN + curr_frag_size;
    ROB_LOGI(fragmentation_log_prefix, "Received part %lu (of %lu - 1), length %lu bytes.", msg_frag_count, frag_msg->fragment_count, curr_frag_size);
    if (expected_message_length != len)
    {
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of fragment %lu: %i bytes, expected %lu", msg_frag_count, len, expected_message_length);
        return;
    }

    // Length of the data checks out
    ROB_LOGI(fragmentation_log_prefix, "Storing fragment: %lu. Offset: %lu, length: %i", msg_frag_count, fragment_size * msg_frag_count, len - FRAG_HEADER_LEN);
    memcpy(frag_msg->data + (frag_msg->fragment_size * msg_frag_count), data + FRAG_HEADER_LEN, len - FRAG_HEADER_LEN);
    frag_msg->received_fragments[msg_frag_count] = 1;

    // Are we at the last fragment, no less?
    // TOOD: Handle an excplicit check request
    if (msg_frag_count == frag_msg->fragment_count - 1)
    {
        ROB_LOGW(fragmentation_log_prefix, "We are on the last fragment");
        // If received parts doesn't add up to fragment count, send list of missing parts to sender
        // TODO: if parts is more than 240 * 8 we can't send that much data. Implicitly that limits the message size to * 250 = 460800 bytes.
        // Note that we probably do not want to handle larger data typically, even though SPIRAM may support it. A larger ESP-NOW frame size would change that though.

        // Check that we have no missing parts
        uint32_t missing_fragments = 0;
        for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
        {
            if (frag_msg->received_fragments[missing_counter] == 0)
            {
                missing_fragments++;
            }
        }
        if (missing_fragments > 0)
        {
            // Gather missing fragments into an array
            ROB_LOGW(fragmentation_log_prefix, "We have %lu missing fragments.", missing_fragments);
            uint8_t *missing = robusto_malloc(FRAG_HEADER_LEN + missing_fragments);
            uint32_t curr_position = FRAG_HEADER_LEN;
            for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
            {
                if (frag_msg->received_fragments[missing_counter] == 0)
                {
                    *(uint32_t *)(missing + curr_position) = frag_msg->received_fragments[missing_counter];
                    curr_position = curr_position + sizeof(curr_position);
                }
            }

            send_message(peer, missing, FRAG_HEADER_LEN + missing_fragments, false);
        }

        // Last, check hash and
        if (frag_msg->hash != robusto_crc32(0, frag_msg->data, frag_msg->data_length))
        {
            ROB_LOGE(fragmentation_log_prefix, "The full message did not match with the hash");
            return;
        }
        else
        {
            ROB_LOGI(fragmentation_log_prefix, "The assembled %lu-byte multimessage matched the hash, passing to incoming.", frag_msg->data_length);
            add_to_history(&peer->espnow_info, false, robusto_handle_incoming(frag_msg->data, frag_msg->data_length, peer, robusto_mt_espnow, 0));
            //TODO: Decide when to remove the fragmented message. It should probably have a "done" property and timeout component. Perhaps a cleanup should be done periodically
        }
    }
}

/**
 * @brief Handled fragmented messages
 *
 * @param peer
 * @param data
 * @param len
 * @param fragment_size The size of the fragment of the media
 */
void handle_fragmented(robusto_peer_t *peer, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message * send_message)
{

    if (data[ROBUSTO_CRC_LENGTH + 1] == FRAG_REQUEST)
    {
        handle_frag_request(peer, data, len, fragment_size);
    }

    if (data[ROBUSTO_CRC_LENGTH + 1] == FRAG_MESSAGE)
    {
        handle_frag_message(peer, data, len, fragment_size, send_message);
    }

    // Postpone QoS so it doesn't interfere with long transmissions.
    peer->espnow_info.postpone_qos = true;
    return;

}

/**
 * @brief Sends a message through ESPNOW.
 */
rob_ret_val_t esp_now_send_message_fragmented(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, uint32_t fragment_size)
{

    int rc = ROB_FAIL;
    // How many parts will it have?
    uint32_t fragment_count = data_length / fragment_size;
    if (fragment_count % fragment_size > 0)
    {
        fragment_count++;
    }

    ROB_LOGI(fragmentation_log_prefix, "Creating and sending a %lu-part, %lu-byte fragmented message in %i-byte chunks.", fragment_count, data_length, fragment_size);

    uint32_t curr_part = 0;
    uint8_t *buffer = robusto_malloc(fragment_size + sizeof(curr_part));

    // First, tell the peer that we are going to send it a fragmented message
    // This is a message saying just that
    fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
    frag_msg->data_length = data_length;
    frag_msg->fragment_count = fragment_count;
    frag_msg->fragment_size = fragment_size;
    frag_msg->hash = robusto_crc32(0, data, data_length);
    // TODO: How big should we allow before SPIRAM and more?
    frag_msg->data = robusto_malloc(frag_msg->data_length);
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);

    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 1, &data_length, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 5, &fragment_count, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 9, &frag_msg->fragment_size, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 13, &frag_msg->hash, 4);
    uint32_t msg_hash = robusto_crc32(0, buffer + 4, 17);
    memcpy(buffer, &msg_hash, 4);

    ROB_LOGI(fragmentation_log_prefix, "ESP-NOW is heartbeat");
    rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 17);
    has_receipt = false;
    if (esp_now_send_check(&(peer->base_mac_address), buffer, ROBUSTO_CRC_LENGTH + 17) != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a failure sending");
        return ROB_FAIL;
    }
    // TODO: Apparently, ESP-NOW has no acknowledgement, we might need to add the same we do for LoRa, for example.

    // We want to wait to make sure the transmission is done.
    int64_t start = r_millis();

    // TODO: Should we have a separate timeout setting here? It is not like a healty ESP-NOW-peer would take more han milliseconds to send a receipt.
    while (!has_receipt && r_millis() < start + 2000)
    {
        robusto_yield();
    }

    if (has_receipt)
    {
        rc = send_status == ROB_OK ? ROB_OK : ROB_FAIL;
        if (rc == ROB_FAIL)
        {
            ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a negative receipt");
            return ROB_FAIL;
        }
        has_receipt = false;
    }
    else
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, timeout.");
        return ROB_ERR_NO_RECEIPT;
    }
    has_receipt = false;
    uint32_t curr_frag_size = ESPNOW_FRAGMENT_SIZE;
    // We always send the same hash, as an identifier
    memcpy(buffer, &frag_msg->hash, 4);
    buffer[ROBUSTO_CRC_LENGTH] = FRAGMENTED_DATA_CONTEXT;
    // TODO: Do this against a map of parts to (re-) send.
    while (curr_part < fragment_count)
    {
        // Counter
        memcpy(buffer + ROBUSTO_CRC_LENGTH + 1, &curr_part, sizeof(curr_part));

        // If it is the last part, send only the remaining data
        if (curr_part == (fragment_count - 1))
        {
            curr_frag_size = data_length - (ESPNOW_FRAGMENT_SIZE * curr_part);
        }

        // Data
        memcpy(buffer + FRAG_HEADER_LEN, data + (ESPNOW_FRAGMENT_SIZE * curr_part), curr_frag_size);
        ROB_LOGI(fragmentation_log_prefix, "Sending part %lu (of %lu), length %lu bytes.", curr_part, fragment_count, curr_frag_size);
        if (esp_now_send_check(&(peer->base_mac_address), buffer, FRAG_HEADER_LEN + curr_frag_size) != ROB_OK)
        {
            // TODO: We might want store failures to resend this
        }
        robusto_yield();
        curr_part++;
    }
    // robusto_free(buffer);

    // TODO: Await response, resend parts if needeed.

    // If still missing parts, .

    return ROB_OK;
}

void robusto_fragment_init(char *_log_prefix)
{
    fragmentation_log_prefix = _log_prefix;
    SLIST_INIT(&fragmented_message_head); /* Initialize the queue */
}
