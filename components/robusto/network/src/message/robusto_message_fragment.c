#include <robusto_message.h>
#include <robusto_network_init.h>
#include <robusto_logging.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include <robusto_states.h>


#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#else
#include <sys/queue.h>
#endif
#include <string.h>


// Define a short cut. CRC + CONTEXT_BYTE + FRAG_TYPE + Fragment counter  the fragment counter uint32_t bytes.
#define FRAG_HEADER_LEN (ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN + 1 + 4)

char *fragmentation_log_prefix = "NOT SET";


SLIST_HEAD(fragmented_message_head_t, fragmented_message);

struct fragmented_message_head_t fragmented_message_head;

// Cache the last frag_msg for optimization
fragmented_message_t *last_frag_msg = NULL;

fragmented_message_t * get_last_frag_message() {
    return last_frag_msg;
}

fragmented_message_t *find_fragmented_message(uint32_t hash)
{
    if ((last_frag_msg) && (last_frag_msg->hash != hash))
    {
        // Not the last fragmented message, find it
        // TODO: Implement this
        ROB_LOGE(fragmentation_log_prefix, "Time to implement looking up the fragment message, existing hash was %" PRIu32 "", last_frag_msg->hash);
        return NULL;
    }
    else if (!last_frag_msg)
    {
        ROB_LOGE(fragmentation_log_prefix, "We are getting fragments without having had a declared fragmented message.");
        return NULL;
    }
    else
    {
        ROB_LOGI(fragmentation_log_prefix, "Matched with cached frag");
        return last_frag_msg;
    }
}

void send_result(robusto_peer_t * peer, fragmented_message_t * frag_msg, rob_ret_val_t return_value, cb_send_message *send_message) {

    uint8_t *buffer = robusto_malloc(ROBUSTO_CRC_LENGTH + 4);
    memcpy(buffer, &frag_msg->hash, 4);
    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESULT;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &return_value, 2);
    send_message(peer, buffer, ROBUSTO_CRC_LENGTH + 4, false);
}

/**
 * @brief Handle a fragmentated message request
 *
 * @param peer
 * @param data
 * @param len
 * @param fragment_size
 */
void handle_frag_request(robusto_peer_t *peer, robusto_media_t *media, const uint8_t *data, int len, uint32_t fragment_size)
{

    // Manually check CRC32 hash
    if (*(uint32_t *)(data) != robusto_crc32(0, data + 4, 18))
    {
        add_to_history(media, false, ROB_FAIL);
        ROB_LOGE(fragmentation_log_prefix, "Fragmented request failed because hash mismatch.");
        return;
    }

    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 14));
    if (!frag_msg)
    {
        frag_msg = robusto_malloc(sizeof(fragmented_message_t));

        frag_msg->fragment_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 6);
        frag_msg->fragment_size = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 10);
        frag_msg->hash = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 14);
    } else {
        ROB_LOGI(fragmentation_log_prefix, "The fragment transmission is already added, assuming same properties. Might be duplicate try or or testing.");
    }

    frag_msg->receive_buffer_length = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 2);
    // TODO: How big should we allow before SPIRAM and more?
    frag_msg->receive_buffer = robusto_malloc(frag_msg->receive_buffer_length);
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    frag_msg->state = ROB_ST_RUNNING;
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);
    
    ROB_LOGI(fragmentation_log_prefix, "Fragmented initialization received, info:\n \
        data_length: %lu bytes, fragment_count: %lu, fragment_size: %lu, hash: %lu.",
             frag_msg->receive_buffer_length, frag_msg->fragment_count, frag_msg->fragment_size, frag_msg->hash);

    /* When the frag_msg was created, a non-used element */
    frag_msg->start_time = r_millis();

    SLIST_INSERT_HEAD(&fragmented_message_head, frag_msg, fragmented_messages);

    last_frag_msg = frag_msg;
    media->last_receive = r_millis();
}

void check_parts(robusto_peer_t *peer, fragmented_message_t *frag_msg, robusto_media_t *media, cb_send_message *send_message)
{

    // Check that we have no missing parts, we do this extra pass to save memory
    uint32_t missing_fragments = 0;
    
    for (uint32_t pass_one_count = 0; pass_one_count < frag_msg->fragment_count; pass_one_count++)
    {
        if (frag_msg->received_fragments[pass_one_count] == 0)
        {
            missing_fragments++;
            frag_msg->last_requested = pass_one_count;
        }
    }
    if (missing_fragments > 0)
    {
        // Gather missing fragments into an array
        ROB_LOGW(fragmentation_log_prefix, "We have %lu missing fragments.", missing_fragments);
        uint8_t *missing = robusto_malloc(ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count);
        memcpy(missing, &frag_msg->hash, 4);
        missing[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
        missing[ROBUSTO_CRC_LENGTH + 1] = FRAG_RESEND;

        for (uint32_t missing_counter = 0; missing_counter < frag_msg->fragment_count; missing_counter++)
        {
            *(uint32_t *)(missing + ROBUSTO_CRC_LENGTH + 2 + missing_counter) = frag_msg->received_fragments[missing_counter];
        }
        //TODO: We probably needs to have CRCs on some medias
        //*(uint32_t *)(missing) = robusto_crc32(0, missing + ROBUSTO_CRC_LENGTH, FRAG_HEADER_LEN + missing_fragments);
        send_message(peer, missing, ROBUSTO_CRC_LENGTH + 2 + frag_msg->fragment_count, false);
        return;
    }
    else
    {
        // Last, check hash and
        if (frag_msg->hash != robusto_crc32(0, frag_msg->receive_buffer, frag_msg->receive_buffer_length))
        {
            ROB_LOGE(fragmentation_log_prefix, "The full message did not match with the hash");
            send_result(peer, frag_msg, ROB_ERR_WRONG_CRC, send_message);
            // TODO: Send check result failed. We have no way of knowing which part failed.
            return;
        }
        else
        {
            ROB_LOGI(fragmentation_log_prefix, "The assembled %lu-byte multimessage matched the hash, passing to incoming.", frag_msg->receive_buffer_length);
            send_result(peer, frag_msg, ROB_OK, send_message);
            add_to_history(media, false, robusto_handle_incoming(frag_msg->receive_buffer, frag_msg->receive_buffer_length, peer, robusto_mt_none, 0));
            // TODO: We'll need the media type here and instead go through that. Or have a callback for the handling of the finished message.
            // TODO: Remove the frag_msg from the list. (handle incoming have already freed the data)
            return;
            
        }
    }
}
void handle_frag_message(robusto_peer_t *peer, robusto_media_t *media, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    // Initiate a new fragmented  (...stream?)
    ROB_LOGI(fragmentation_log_prefix, "handle_frag_message (hash %lu):", *(uint32_t *)data);
    if (len > ROBUSTO_CRC_LENGTH + 18) {
        rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, ROBUSTO_CRC_LENGTH + 18);
    } else {
        rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, len);
    }    
    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }

    uint32_t msg_frag_count = *(uint32_t *)(data + ROBUSTO_CRC_LENGTH + 2);

    // We check the length of all fragments, start with calculating what the current should be
    uint32_t curr_frag_size = frag_msg->fragment_size;
    if (msg_frag_count == frag_msg->fragment_count - 1)
    {
        // If it is the last fragment, it is whatever is left
        curr_frag_size = frag_msg->receive_buffer_length - (frag_msg->fragment_size * msg_frag_count);
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
    memcpy(frag_msg->receive_buffer + (frag_msg->fragment_size * msg_frag_count), data + FRAG_HEADER_LEN, len - FRAG_HEADER_LEN);
    frag_msg->received_fragments[msg_frag_count] = 1;

    // Are we at the last, or last requested, fragment, no less?
    if ((frag_msg->last_requested > 0 && msg_frag_count == frag_msg->last_requested) || 
        (msg_frag_count == frag_msg->fragment_count - 1))
    {
        ROB_LOGW(fragmentation_log_prefix, "We are on the last fragment (initial send or re-send)");
        // If received parts doesn't add up to fragment count, send list of missing parts to sender
        // TODO: if parts is more than 240 * 8 we can't send that much data. Implicitly that limits the message size to * 250 = 460800 bytes.
        // Note that we probably do not want to handle larger data typically, even though SPIRAM may support it. A larger ESP-NOW frame size would change that though.

        check_parts(peer, frag_msg, media, send_message);
    }
}

void send_fragments(robusto_peer_t *peer, fragmented_message_t *frag_msg, cb_send_message *send_message)
{
    uint8_t *buffer = robusto_malloc(frag_msg->fragment_size + FRAG_HEADER_LEN);

    ROB_LOGI(fragmentation_log_prefix, "Send fragments (%" PRIu32 "):", frag_msg->fragment_count);
    //rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, data, data_length);

    // TODO: Apparently, ESP-NOW has no acknowledgement, we might need to add the same we do for LoRa, for example.

    uint32_t curr_frag_size = frag_msg->fragment_size;
    // We always send the same hash, as an identifier
    memcpy(buffer, &frag_msg->hash, 4);
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_MESSAGE;

    for (uint32_t curr_fragment = 0; curr_fragment < frag_msg->fragment_count; curr_fragment++)
    {
        if (frag_msg->received_fragments[curr_fragment] == 1)
        {
            ROB_LOGI(fragmentation_log_prefix, "Skipping part %lu", curr_fragment);
            continue;
        }
        // Counter
        memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &curr_fragment, sizeof(curr_fragment));

        // If it is the last part, send only the remaining data
        if (curr_fragment == (frag_msg->fragment_count - 1))
        {
            curr_frag_size = frag_msg->send_data_length - (frag_msg->fragment_size * curr_fragment);
        }

        ROB_LOGI(fragmentation_log_prefix, "Sending fragment %lu (of %lu), pos %lu, length %lu bytes of (%lu total bytes).", 
            curr_fragment, frag_msg->fragment_count, frag_msg->fragment_size * curr_fragment, curr_frag_size, frag_msg->send_data_length);
        memcpy(buffer + FRAG_HEADER_LEN, frag_msg->send_data + (frag_msg->fragment_size * curr_fragment), curr_frag_size);
        if (send_message(&(peer->base_mac_address), buffer, FRAG_HEADER_LEN + curr_frag_size, false) != ROB_OK)
        {
            ROB_LOGE(fragmentation_log_prefix, "Failed sending fragment (%" PRIu32 "):", curr_fragment);
            // TODO: We might want store failures to resend this
        }
        robusto_yield();
    }
}

/**
 * @brief Handle the result message
 *
 * @param peer
 * @param media
 * @param data
 * @param len
 * @param fragment_size
 * @param send_message
 */

void handle_frag_resend(robusto_peer_t *peer, robusto_media_t *media, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_resend");

    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_resend, fragment count: %lu ", frag_msg->fragment_count);
    frag_msg->state = ROB_ST_RETRYING;
    if (len - (ROBUSTO_CRC_LENGTH + 2) != frag_msg->fragment_count)
    {
        ROB_LOGE(fragmentation_log_prefix, "Wrong length of missing fragments data", len - (ROBUSTO_CRC_LENGTH + 2), frag_msg->fragment_count);
        media->receive_failures++;
        return;
    }
    media->receive_successes++;
    memcpy(frag_msg->received_fragments, data + ROBUSTO_CRC_LENGTH + 2, len - (ROBUSTO_CRC_LENGTH + 2));
    send_fragments(peer, frag_msg, send_message);
}


/**
 * @brief Handle the success message
 *
 * @param peer
 * @param media
 * @param data
 * @param len
 * @param fragment_size
 * @param send_message
 */

void handle_frag_result(robusto_peer_t *peer, robusto_media_t *media, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    ROB_LOGI(fragmentation_log_prefix, "In handle_frag_result");

    fragmented_message_t *frag_msg = find_fragmented_message(*(uint32_t *)data);
    if (!frag_msg)
    {
        return;
    }
    rob_ret_val_t result;
    memcpy(&result, data + ROBUSTO_CRC_LENGTH + 2, 2);
   

    switch (result) {
        case ROB_OK:
            ROB_LOGI(fragmentation_log_prefix, "Receiver reports successful transmisstion.");
            frag_msg->state = ROB_ST_SUCCEEDED;
            break;
        case ROB_FAIL:
            ROB_LOGE(fragmentation_log_prefix, "Receiver reports unsuccessful successful transmisstion.");
            frag_msg->state = ROB_ST_FAILED;
            break;
        case ROB_ERR_WRONG_CRC:
            ROB_LOGE(fragmentation_log_prefix, "Receiver reports CRC mismatch");
            frag_msg->state = ROB_ST_FAILED;
            break;
        case ROB_ERR_TIMEOUT:
            ROB_LOGE(fragmentation_log_prefix, "Receiver reports timeout ");
            frag_msg->state = ROB_ST_TIMED_OUT;
            break;
        default:
            ROB_LOGE(fragmentation_log_prefix, "Unhandled fragmentation result code: %i", result);
            break;
        
    }
    
}

/**
 * @brief Handled fragmented messages
 *
 * @param peer The sending peer
 * @param data The fragment data
 * @param len The length of the data
 * @param fragment_size The size of the fragment of the media
 */
void handle_fragmented(robusto_peer_t *peer, robusto_media_t *media, const uint8_t *data, int len, uint32_t fragment_size, cb_send_message *send_message)
{
    switch (data[ROBUSTO_CRC_LENGTH + 1])
    {
    case FRAG_REQUEST:
        handle_frag_request(peer, media, data, len, fragment_size);
        break;
    case FRAG_MESSAGE:
        handle_frag_message(peer, media, data, len, fragment_size, send_message);
        break;
    case FRAG_RESEND:
        handle_frag_resend(peer, media, data, len, fragment_size, send_message);
        break;
    case FRAG_RESULT:
        handle_frag_result(peer, media, data, len, fragment_size, send_message);
        break;
    default:
        ROB_LOGE(fragmentation_log_prefix, "Invalid fragment type byte: %hu", data[ROBUSTO_CRC_LENGTH + 1]);
    }
    // Postpone QoS so it doesn't interfere with long transmissions.
    media->postpone_qos = true;
    return;
}

/**
 * @brief Sends a fragmented message
 */
rob_ret_val_t send_message_fragmented(robusto_peer_t *peer, robusto_media_t *media, uint8_t *data, uint32_t data_length, uint32_t fragment_size, cb_send_message *send_message)
{
    int rc = ROB_FAIL;
    // How many parts will it have?
    uint32_t fragment_count = data_length / fragment_size;
    if (fragment_count % fragment_size > 0)
    {
        fragment_count++;
    }

    ROB_LOGI(fragmentation_log_prefix, "Creating and sending a %lu-part, %lu-byte fragmented message in %lu-byte chunks.", fragment_count, data_length, fragment_size);

    uint32_t curr_part = 0;
    uint8_t *buffer = robusto_malloc(fragment_size + sizeof(curr_part));

    // First, tell the peer that we are going to send it a fragmented message
    // This is a message saying just that
    fragmented_message_t *frag_msg = robusto_malloc(sizeof(fragmented_message_t));
    frag_msg->send_data_length = data_length;
    frag_msg->send_data = data;
    frag_msg->fragment_count = fragment_count;
    frag_msg->fragment_size = fragment_size;
    frag_msg->hash = robusto_crc32(0, data, data_length);
    

    frag_msg->state = ROB_ST_RUNNING;
    frag_msg->received_fragments = robusto_malloc(frag_msg->fragment_count);
    memset(frag_msg->received_fragments, 0, frag_msg->fragment_count);

    // Encode into a message
    buffer[ROBUSTO_CRC_LENGTH] = MSG_FRAGMENTED;
    buffer[ROBUSTO_CRC_LENGTH + 1] = FRAG_REQUEST;
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 2, &data_length, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 6, &fragment_count, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 10, &frag_msg->fragment_size, 4);
    memcpy(buffer + ROBUSTO_CRC_LENGTH + 14, &frag_msg->hash, 4);

    last_frag_msg = frag_msg;

    uint32_t msg_hash = robusto_crc32(0, buffer + 4, 18);
    memcpy(buffer, &msg_hash, 4);

    ROB_LOGI(fragmentation_log_prefix, "Sending a fragment request:");
    rob_log_bit_mesh(ROB_LOG_INFO, fragmentation_log_prefix, buffer, ROBUSTO_CRC_LENGTH + 18);

    if (send_message(&(peer->base_mac_address), buffer, ROBUSTO_CRC_LENGTH + 18, true) != ROB_OK)
    {
        ROB_LOGE(fragmentation_log_prefix, "Could not initiate fragmented messaging, got a failure sending");
        return ROB_FAIL;
    }
    // TODO: Apparently, ESP-NOW has no acknowledgement, we might need to add the same we do for LoRa, for example.

    send_fragments(peer, frag_msg, send_message);

    return ROB_OK;
}

void robusto_message_fragment_init(char *_log_prefix)
{
    fragmentation_log_prefix = _log_prefix;
    SLIST_INIT(&fragmented_message_head); /* Initialize the queue */
}
