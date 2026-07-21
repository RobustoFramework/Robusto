#include "robusto_proxy_client.h"

#include <stdbool.h>
#include <string.h>

#include "robusto_proxy_control.h"
#include "robusto_proxy_frame.h"
#include "robusto_proxy_pubsub_client.h"

static uint64_t nonzero_u64(uint64_t value)
{
    return value == 0U ? 1U : value;
}

static uint8_t maximum_retries(uint8_t domain, uint8_t opcode, bool mutation)
{
    if (mutation)
    {
        return 0U;
    }
    if (domain == ROBUSTO_PROXY_DOMAIN_CONTROL && opcode == ROBUSTO_PROXY_OPCODE_HELLO)
    {
        return 4U;
    }
    return 1U;
}

static uint32_t retry_delay_ms(
    robusto_proxy_client_t *client,
    uint8_t domain,
    uint8_t opcode,
    uint8_t retry_index,
    uint32_t retry_after_ms)
{
    static const uint32_t handshake_backoff[] = {100U, 250U, 500U, 1000U};
    static const uint32_t normal_backoff[] = {50U, 200U};
    uint32_t delay;
    uint32_t jitter;

    if (retry_after_ms > 0U)
    {
        delay = retry_after_ms;
    }
    else if (domain == ROBUSTO_PROXY_DOMAIN_CONTROL && opcode == ROBUSTO_PROXY_OPCODE_HELLO)
    {
        delay = handshake_backoff[retry_index];
    }
    else
    {
        delay = normal_backoff[retry_index < 2U ? retry_index : 1U];
    }
    jitter = client->retry_jitter_ms(client->clock_context, 20U);
    if (jitter > 20U)
    {
        jitter = 20U;
    }
    return delay + jitter;
}

rob_ret_val_t robusto_proxy_status_to_robusto(uint16_t status)
{
    switch (status)
    {
        case ROBUSTO_PROXY_STATUS_OK:
            return ROB_OK;
        case ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT:
        case ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD:
        case ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID:
            return ROB_ERR_INVALID_ARG;
        case ROBUSTO_PROXY_STATUS_OUT_OF_MEMORY:
            return ROB_ERR_OUT_OF_MEMORY;
        case ROBUSTO_PROXY_STATUS_BUSY:
        case ROBUSTO_PROXY_STATUS_TOO_MANY_IN_FLIGHT:
        case ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_LIMIT:
            return ROB_ERR_QUEUE_FULL;
        case ROBUSTO_PROXY_STATUS_TIMEOUT:
            return ROB_ERR_TIMEOUT;
        case ROBUSTO_PROXY_STATUS_NOT_READY:
        case ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED:
        case ROBUSTO_PROXY_STATUS_STALE_SESSION:
            return ROB_ERR_NOT_READY;
        case ROBUSTO_PROXY_STATUS_UNSUPPORTED_VERSION:
        case ROBUSTO_PROXY_STATUS_UNSUPPORTED_DOMAIN:
        case ROBUSTO_PROXY_STATUS_UNSUPPORTED_OPCODE:
        case ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE:
            return ROB_ERR_NOT_SUPPORTED;
        case ROBUSTO_PROXY_STATUS_NOT_FOUND:
        case ROBUSTO_PROXY_STATUS_PUBSUB_SUBSCRIPTION_NOT_FOUND:
            return ROB_ERR_INVALID_ID;
        case ROBUSTO_PROXY_STATUS_LINK_CRC:
            return ROB_ERR_WRONG_CRC;
        case ROBUSTO_PROXY_STATUS_LINK_LENGTH:
        case ROBUSTO_PROXY_STATUS_PUBSUB_PAYLOAD_TOO_LARGE:
            return ROB_ERR_MESSAGE_TOO_LONG;
        case ROBUSTO_PROXY_STATUS_LINK_IO:
            return ROB_ERR_SEND_FAIL;
        case ROBUSTO_PROXY_STATUS_OUTCOME_UNKNOWN:
            return ROB_ERR_OUTCOME_UNKNOWN;
        case ROBUSTO_PROXY_STATUS_INTERNAL:
        case ROBUSTO_PROXY_STATUS_CONFLICT:
        case ROBUSTO_PROXY_STATUS_PUBSUB_DELIVERY_FAILED:
        default:
            return ROB_FAIL;
    }
}

rob_ret_val_t robusto_proxy_client_init(
    robusto_proxy_client_t *client,
    const robusto_proxy_client_config_t *config)
{
    if (client == NULL || config == NULL || config->controller_boot_id == 0U ||
        config->request_timeout_ms == 0U || config->exchange == NULL ||
        config->now_ms == NULL || config->wait_ms == NULL ||
        config->retry_jitter_ms == NULL || config->request_frame == NULL ||
        config->request_frame_size < ROBUSTO_PROXY_SLOT_SIZE_BYTES ||
        config->response_frame == NULL ||
        config->response_frame_size < ROBUSTO_PROXY_SLOT_SIZE_BYTES)
    {
        return ROB_ERR_INVALID_ARG;
    }

    memset(client, 0, sizeof(*client));
    robusto_proxy_session_init(&client->session, config->profile,
                               config->controller_boot_id,
                               config->correlation_seed,
                               config->sequence_seed);
    robusto_proxy_inflight_init(&client->inflight,
                                client->session.local_limits.max_in_flight);
    client->exchange = config->exchange;
    client->transport_context = config->transport_context;
    client->now_ms = config->now_ms;
    client->wait_ms = config->wait_ms;
    client->retry_jitter_ms = config->retry_jitter_ms;
    client->clock_context = config->clock_context;
    client->request_frame = config->request_frame;
    client->request_frame_size = config->request_frame_size;
    client->response_frame = config->response_frame;
    client->response_frame_size = config->response_frame_size;
    client->next_operation_id = nonzero_u64(config->operation_seed);
    client->request_timeout_ms = config->request_timeout_ms;
    return ROB_OK;
}

uint64_t robusto_proxy_client_take_operation_id(robusto_proxy_client_t *client)
{
    uint64_t operation_id;

    if (client == NULL)
    {
        return 0U;
    }
    operation_id = client->next_operation_id;
    client->next_operation_id = nonzero_u64(operation_id + 1U);
    return operation_id;
}

rob_ret_val_t robusto_proxy_client_request(
    robusto_proxy_client_t *client,
    uint8_t domain,
    uint8_t opcode,
    const uint8_t *payload,
    size_t payload_size,
    bool mutation,
    const uint8_t **success_payload,
    size_t *success_payload_size)
{
    robusto_proxy_frame_header_t request_header;
    const robusto_proxy_frame_header_t *response_header;
    robusto_proxy_response_prefix_t prefix;
    robusto_proxy_inflight_entry_t *inflight_entry;
    uint32_t correlation_id;
    uint32_t sequence;
    uint32_t now_ms;
    size_t request_size;
    size_t response_size;
    size_t response_payload_offset;
    size_t expected_response_size;
    uint8_t retry_limit;
    uint8_t attempt;
    rob_ret_val_t result = ROB_FAIL;
    rob_ret_val_t transport_result;

    if (client == NULL || (payload_size > 0U && payload == NULL) ||
        payload_size > ROBUSTO_PROXY_MAX_PAYLOAD_BYTES ||
        success_payload == NULL || success_payload_size == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    if (!(domain == ROBUSTO_PROXY_DOMAIN_CONTROL && opcode == ROBUSTO_PROXY_OPCODE_HELLO) &&
        client->session.state != ROBUSTO_PROXY_SESSION_ESTABLISHED)
    {
        return ROB_ERR_NOT_READY;
    }

    *success_payload = NULL;
    *success_payload_size = 0U;
    client->last_status = 0U;
    client->last_result_flags = 0U;
    client->last_retry_after_ms = 0U;
    correlation_id = robusto_proxy_session_take_correlation_id(&client->session);
    sequence = robusto_proxy_session_take_sequence(&client->session);
    now_ms = client->now_ms(client->clock_context);
    if (robusto_proxy_inflight_begin(&client->inflight, domain, opcode,
                                     correlation_id, sequence, now_ms,
                                     client->request_timeout_ms,
                                     client->session.peer_boot_id) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_CONV_LIST_FULL;
    }
    retry_limit = maximum_retries(domain, opcode, mutation);
    for (attempt = 0U; attempt <= retry_limit; ++attempt)
    {
        robusto_proxy_transfer_acceptance_t acceptance =
            ROBUSTO_PROXY_TRANSFER_NOT_ACCEPTED;
        uint8_t flags = ROBUSTO_PROXY_FLAG_REQUEST;

        if (attempt > 0U)
        {
            sequence = robusto_proxy_session_take_sequence(&client->session);
            flags |= ROBUSTO_PROXY_FLAG_RETRY;
        }
        now_ms = client->now_ms(client->clock_context);
        inflight_entry = robusto_proxy_inflight_find(&client->inflight, correlation_id);
        if (inflight_entry == NULL)
        {
            result = ROB_FAIL;
            break;
        }
        inflight_entry->sequence = sequence;
        inflight_entry->started_at_ms = now_ms;
        robusto_proxy_frame_header_init(&request_header, flags, domain, opcode,
                                        correlation_id, sequence,
                                        (uint32_t)payload_size);
        if (robusto_proxy_frame_encode(
                client->request_frame, client->request_frame_size,
                &request_header, payload, &request_size) != ROBUSTO_PROXY_RESULT_OK)
        {
            result = ROB_ERR_INVALID_ARG;
            break;
        }

        response_size = 0U;
        transport_result = client->exchange(
            client->transport_context, client->request_frame, request_size,
            client->response_frame, client->response_frame_size, &response_size,
            client->request_timeout_ms, &acceptance);
        if (transport_result != ROB_OK)
        {
            result = mutation && acceptance != ROBUSTO_PROXY_TRANSFER_NOT_ACCEPTED
                         ? ROB_ERR_OUTCOME_UNKNOWN
                         : transport_result;
            if (!mutation && attempt < retry_limit)
            {
                client->wait_ms(client->clock_context,
                                retry_delay_ms(client, domain, opcode, attempt, 0U));
                client->retries += 1U;
                continue;
            }
            break;
        }

        if (acceptance != ROBUSTO_PROXY_TRANSFER_ACCEPTED ||
            robusto_proxy_frame_validate_buffer(client->response_frame,
                                                 response_size, NULL) !=
                ROBUSTO_PROXY_RESULT_OK)
        {
            result = mutation ? ROB_ERR_OUTCOME_UNKNOWN : ROB_ERR_PARSING_FAILED;
            if (!mutation && attempt < retry_limit)
            {
                client->wait_ms(client->clock_context,
                                retry_delay_ms(client, domain, opcode, attempt, 0U));
                client->retries += 1U;
                continue;
            }
            break;
        }
        response_header = (const robusto_proxy_frame_header_t *)client->response_frame;
        expected_response_size = robusto_proxy_frame_size_bytes(response_header->payload_length);
        if (response_size != expected_response_size ||
            response_header->flags != ROBUSTO_PROXY_FLAG_RESPONSE ||
            response_header->domain != domain || response_header->opcode != opcode ||
            response_header->correlation_id != correlation_id ||
            robusto_proxy_decode_response_prefix(
                client->response_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
                response_header->payload_length, &prefix) != ROBUSTO_PROXY_RESULT_OK)
        {
            result = mutation ? ROB_ERR_OUTCOME_UNKNOWN : ROB_ERR_PARSING_FAILED;
            if (!mutation && attempt < retry_limit)
            {
                client->wait_ms(client->clock_context,
                                retry_delay_ms(client, domain, opcode, attempt, 0U));
                client->retries += 1U;
                continue;
            }
            break;
        }

        client->last_status = prefix.status;
        client->last_result_flags = prefix.result_flags;
        client->last_retry_after_ms = prefix.retry_after_ms;
        if (!robusto_proxy_response_prefix_is_success(&prefix))
        {
            result = robusto_proxy_status_to_robusto(prefix.status);
            if (!mutation &&
                (prefix.result_flags & ROBUSTO_PROXY_RESULT_FLAG_RETRYABLE) != 0U &&
                attempt < retry_limit)
            {
                client->wait_ms(
                    client->clock_context,
                    retry_delay_ms(client, domain, opcode, attempt,
                                   prefix.retry_after_ms));
                client->retries += 1U;
                continue;
            }
            break;
        }
        response_payload_offset = ROBUSTO_PROXY_HEADER_SIZE_BYTES +
                                  ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES +
                                  prefix.detail_length;
        *success_payload = client->response_frame + response_payload_offset;
        *success_payload_size = response_header->payload_length -
                                ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES -
                                prefix.detail_length;
        result = ROB_OK;
        break;
    }
    (void)robusto_proxy_inflight_complete(&client->inflight, correlation_id);
    return result;
}

rob_ret_val_t robusto_proxy_client_connect(robusto_proxy_client_t *client)
{
    robusto_proxy_hello_request_t request;
    robusto_proxy_hello_response_t response;
    const uint8_t *response_payload;
    size_t response_payload_size;
    uint64_t previous_peer_boot_id;
    rob_ret_val_t result;

    if (client == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    previous_peer_boot_id = client->session.peer_boot_id;
    memset(&request, 0, sizeof(request));
    request.controller_boot_id = client->session.local_boot_id;
    request.min_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    request.max_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
    request.min_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
    request.max_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
    request.max_payload = ROBUSTO_PROXY_MAX_PAYLOAD_BYTES;
    request.max_in_flight = client->session.local_limits.max_in_flight;
    request.required_features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    if (robusto_proxy_encode_hello_request(client->response_frame,
                                           client->response_frame_size,
                                           &request) != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_FAIL;
    }

    client->session.state = ROBUSTO_PROXY_SESSION_NEGOTIATING;
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_CONTROL, ROBUSTO_PROXY_OPCODE_HELLO,
        client->response_frame, ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES, false,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_RESET;
        return result;
    }
    if (robusto_proxy_decode_hello_response(response_payload, response_payload_size,
                                            &response) != ROBUSTO_PROXY_RESULT_OK ||
        (response.enabled_features & ROBUSTO_PROXY_FEATURE_PUBSUB_V1) == 0U ||
        !robusto_proxy_session_apply_hello_response(&client->session, &response))
    {
        client->session.state = ROBUSTO_PROXY_SESSION_INCOMPATIBLE;
        return ROB_ERR_NOT_SUPPORTED;
    }
    robusto_proxy_inflight_init(&client->inflight,
                                client->session.negotiated_limits.max_in_flight);
    client->consecutive_health_timeouts = 0U;
    if (previous_peer_boot_id != 0U &&
        previous_peer_boot_id != client->session.peer_boot_id)
    {
        robusto_proxy_pubsub_session_reset(client);
    }
    result = robusto_proxy_pubsub_reconcile(client);
    if (result != ROB_OK)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_DEGRADED;
    }
    return result;
}

rob_ret_val_t robusto_proxy_client_query_capabilities(
    robusto_proxy_client_t *client,
    robusto_proxy_capability_response_t *response)
{
    const uint8_t *response_payload;
    size_t response_payload_size;
    rob_ret_val_t result;

    if (client == NULL || response == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_CONTROL,
        ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY, NULL, 0U, false,
        &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        return result;
    }
    if (robusto_proxy_decode_capability_response(
            response_payload, response_payload_size, response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    if (response->proxy_boot_id != client->session.peer_boot_id)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_RESET;
        robusto_proxy_pubsub_session_reset(client);
        return ROB_ERR_NOT_READY;
    }
    return ROB_OK;
}

rob_ret_val_t robusto_proxy_client_query_health(
    robusto_proxy_client_t *client,
    robusto_proxy_health_response_t *response)
{
    const uint8_t *response_payload;
    size_t response_payload_size;
    rob_ret_val_t result;

    if (client == NULL || response == NULL)
    {
        return ROB_ERR_INVALID_ARG;
    }
    result = robusto_proxy_client_request(
        client, ROBUSTO_PROXY_DOMAIN_CONTROL, ROBUSTO_PROXY_OPCODE_HEALTH,
        NULL, 0U, false, &response_payload, &response_payload_size);
    if (result != ROB_OK)
    {
        if (result == ROB_ERR_TIMEOUT)
        {
            client->consecutive_health_timeouts += 1U;
            if (client->consecutive_health_timeouts >= 3U)
            {
                client->session.state = ROBUSTO_PROXY_SESSION_DEGRADED;
            }
        }
        return result;
    }
    client->consecutive_health_timeouts = 0U;
    if (robusto_proxy_decode_health_response(
            response_payload, response_payload_size, response) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return ROB_ERR_PARSING_FAILED;
    }
    if (response->proxy_boot_id != client->session.peer_boot_id)
    {
        client->session.state = ROBUSTO_PROXY_SESSION_RESET;
        robusto_proxy_pubsub_session_reset(client);
        return ROB_ERR_NOT_READY;
    }
    return ROB_OK;
}