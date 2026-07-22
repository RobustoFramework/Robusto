#include "robusto_proxy_service.h"

#include <string.h>

#include "robusto_proxy_control.h"
#include "robusto_proxy_frame.h"

static uint64_t available_features(const robusto_proxy_service_t *service)
{
    uint64_t features = ROBUSTO_PROXY_FEATURE_PUBSUB_V1;
    const robusto_proxy_pubsub_adapter_t *adapter = service->pubsub_adapter;

    if (adapter != NULL && adapter->publish_begin != NULL &&
        adapter->publish_chunk != NULL && adapter->publish_commit != NULL &&
        adapter->publish_abort != NULL && adapter->session_reset != NULL)
    {
        features |= ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH;
    }
    return features;
}

void robusto_proxy_service_init(
    robusto_proxy_service_t *service,
    robusto_proxy_profile_t profile,
    uint64_t local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed,
    uint16_t negotiated_inflight,
    uint32_t now_ms)
{
    if (service == NULL)
    {
        return;
    }

    memset(service, 0, sizeof(*service));
    robusto_proxy_session_init(
        &service->session,
        profile,
        local_boot_id,
        correlation_seed,
        sequence_seed);
    robusto_proxy_inflight_init(&service->inflight, negotiated_inflight);
    service->started_at_ms = now_ms;
}

void robusto_proxy_service_set_pubsub_adapter(
    robusto_proxy_service_t *service,
    const robusto_proxy_pubsub_adapter_t *adapter,
    void *context)
{
    if (service == NULL)
    {
        return;
    }
    service->pubsub_adapter = adapter;
    service->pubsub_adapter_context = context;
}

uint16_t robusto_proxy_service_tick(
    robusto_proxy_service_t *service,
    uint32_t now_ms)
{
    uint16_t expired;

    if (service == NULL)
    {
        return 0U;
    }

    expired = robusto_proxy_inflight_expire(&service->inflight, now_ms);
    if (expired > 0U)
    {
        (void)robusto_proxy_session_note_expired_requests(&service->session, expired);
    }

    return expired;
}

bool robusto_proxy_service_build_health_response(
    const robusto_proxy_service_t *service,
    uint32_t now_ms,
    robusto_proxy_health_response_t *response)
{
    if (service == NULL || response == NULL)
    {
        return false;
    }

    memset(response, 0, sizeof(*response));
    response->proxy_boot_id = service->session.local_boot_id;
    response->uptime_ms = now_ms - service->started_at_ms;
    response->requests = service->requests;
    response->events = service->events;
    response->errors = service->errors;
    response->rx_crc_errors = service->rx_crc_errors;
    response->queue_high_water = service->queue_high_water;
    return robusto_proxy_session_apply_health_metrics(&service->session, response);
}

bool robusto_proxy_service_handle_control_request(
    robusto_proxy_service_t *service,
    uint8_t opcode,
    const uint8_t *request_payload,
    size_t request_payload_size,
    uint32_t now_ms,
    uint8_t *response_buffer,
    size_t response_buffer_size,
    size_t *response_size)
{
    robusto_proxy_response_prefix_t prefix;

    if (service == NULL || response_buffer == NULL || response_size == NULL)
    {
        return false;
    }

    memset(&prefix, 0, sizeof(prefix));

    if (opcode == ROBUSTO_PROXY_OPCODE_HELLO)
    {
        robusto_proxy_hello_request_t request;
        robusto_proxy_hello_response_t response;
        uint16_t error_status = ROBUSTO_PROXY_STATUS_OK;
        uint32_t local_max_payload = service->session.local_limits.request_pool_bytes;
        uint64_t requested_features;
        uint64_t supported_features = available_features(service);

        if (response_buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES)
        {
            return false;
        }

        service->requests += 1U;
        if (request_payload == NULL ||
            request_payload_size != ROBUSTO_PROXY_HELLO_REQUEST_SIZE_BYTES ||
            robusto_proxy_decode_hello_request(request_payload, request_payload_size, &request) != ROBUSTO_PROXY_RESULT_OK ||
            request.controller_boot_id == 0U ||
            request.reserved != 0U ||
            request.min_protocol_major > request.max_protocol_major ||
            request.min_protocol_minor > request.max_protocol_minor ||
            request.max_payload == 0U ||
            request.max_payload > ROBUSTO_PROXY_MAX_PAYLOAD_BYTES ||
            request.max_in_flight == 0U ||
            request.max_in_flight > ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS)
        {
            error_status = ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (request.min_protocol_major > ROBUSTO_PROXY_PROTOCOL_MAJOR ||
                 request.max_protocol_major < ROBUSTO_PROXY_PROTOCOL_MAJOR ||
                 request.min_protocol_minor > ROBUSTO_PROXY_PROTOCOL_MINOR)
        {
            error_status = ROBUSTO_PROXY_STATUS_UNSUPPORTED_VERSION;
        }
            else if ((request.required_features & ~supported_features) != 0U)
        {
            error_status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }

        if (error_status != ROBUSTO_PROXY_STATUS_OK)
        {
            prefix.status = error_status;
            if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }

            service->errors += 1U;
            *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;
            return true;
        }

        if (response_buffer_size < (ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES))
        {
            service->requests -= 1U;
            return false;
        }

        if (service->pubsub_adapter != NULL &&
            service->pubsub_adapter->session_reset != NULL)
        {
            uint16_t reset_status = service->pubsub_adapter->session_reset(
                service->pubsub_adapter_context);
            if (reset_status != ROBUSTO_PROXY_STATUS_OK)
            {
                prefix.status = reset_status;
                if (robusto_proxy_encode_response_prefix(
                        response_buffer, response_buffer_size, &prefix) !=
                    ROBUSTO_PROXY_RESULT_OK)
                {
                    service->requests -= 1U;
                    return false;
                }
                service->errors += 1U;
                *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;
                return true;
            }
        }

        if (local_max_payload > ROBUSTO_PROXY_MAX_PAYLOAD_BYTES)
        {
            local_max_payload = ROBUSTO_PROXY_MAX_PAYLOAD_BYTES;
        }

        memset(&response, 0, sizeof(response));
        response.proxy_boot_id = service->session.local_boot_id;
        response.selected_protocol_major = ROBUSTO_PROXY_PROTOCOL_MAJOR;
        response.selected_protocol_minor = ROBUSTO_PROXY_PROTOCOL_MINOR;
        response.selected_max_payload = request.max_payload < local_max_payload
                                            ? request.max_payload
                                            : local_max_payload;
        response.selected_max_in_flight = request.max_in_flight < service->session.local_limits.max_in_flight
                                              ? request.max_in_flight
                                              : service->session.local_limits.max_in_flight;
        response.dedupe_entries = service->session.local_limits.dedupe_entries;
        response.dedupe_window_ms = service->session.local_limits.dedupe_window_ms;
        requested_features = request.required_features | request.optional_features;
        response.enabled_features =
            requested_features & supported_features;
        response.proxy_uptime_ms = now_ms - service->started_at_ms;

        if (service->session.state == ROBUSTO_PROXY_SESSION_ESTABLISHED &&
            service->session.peer_boot_id == request.controller_boot_id)
        {
            response.selected_protocol_major = service->session.selected_protocol_major;
            response.selected_protocol_minor = service->session.selected_protocol_minor;
            response.selected_max_payload = service->session.negotiated_limits.request_pool_bytes;
            response.selected_max_in_flight = service->session.negotiated_limits.max_in_flight;
            response.dedupe_entries = service->session.negotiated_limits.dedupe_entries;
            response.dedupe_window_ms = service->session.negotiated_limits.dedupe_window_ms;
            response.enabled_features =
                service->session.enabled_features & supported_features;
        }

        prefix.status = ROBUSTO_PROXY_STATUS_OK;
        if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK ||
            robusto_proxy_encode_hello_response(
                response_buffer + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                &response) != ROBUSTO_PROXY_RESULT_OK)
        {
            service->requests -= 1U;
            return false;
        }

        if (service->session.state != ROBUSTO_PROXY_SESSION_ESTABLISHED ||
            service->session.peer_boot_id != request.controller_boot_id)
        {
            robusto_proxy_inflight_init(&service->inflight, response.selected_max_in_flight);
            service->session.peer_boot_id = request.controller_boot_id;
            service->session.enabled_features = response.enabled_features;
            service->session.selected_protocol_major = response.selected_protocol_major;
            service->session.selected_protocol_minor = response.selected_protocol_minor;
            service->session.negotiated_limits.max_in_flight = response.selected_max_in_flight;
            service->session.negotiated_limits.max_subscriptions = service->session.local_limits.max_subscriptions;
            service->session.negotiated_limits.dedupe_entries = response.dedupe_entries;
            service->session.negotiated_limits.dedupe_window_ms = response.dedupe_window_ms;
            service->session.negotiated_limits.request_pool_bytes = response.selected_max_payload;
            service->session.negotiated_limits.response_pool_bytes = response.selected_max_payload;
            service->session.state = ROBUSTO_PROXY_SESSION_ESTABLISHED;
        }

        *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HELLO_RESPONSE_SIZE_BYTES;
        return true;
    }

    if (opcode == ROBUSTO_PROXY_OPCODE_HEALTH)
    {
        robusto_proxy_health_response_t response;

        if (request_payload_size != 0U ||
            response_buffer_size < (ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES))
        {
            return false;
        }

        (void)request_payload;
        service->requests += 1U;
        if (!robusto_proxy_service_build_health_response(service, now_ms, &response))
        {
            service->requests -= 1U;
            return false;
        }

        prefix.status = ROBUSTO_PROXY_STATUS_OK;
        if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK)
        {
            return false;
        }

        if (robusto_proxy_encode_health_response(
                response_buffer + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                &response) != ROBUSTO_PROXY_RESULT_OK)
        {
            return false;
        }

        *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES;
        return true;
    }

    if (opcode == ROBUSTO_PROXY_OPCODE_CAPABILITY_QUERY)
    {
        robusto_proxy_capability_response_t response;

        if (request_payload_size != 0U ||
            response_buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES)
        {
            return false;
        }

        (void)request_payload;
        if (service->session.state != ROBUSTO_PROXY_SESSION_ESTABLISHED)
        {
            prefix.status = ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED;
            if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK)
            {
                return false;
            }

            service->errors += 1U;
            *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;
            return true;
        }

        if (response_buffer_size < (ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES))
        {
            return false;
        }

        memset(&response, 0, sizeof(response));
        response.proxy_boot_id = service->session.local_boot_id;
        response.enabled_features = service->session.enabled_features;
        if ((response.enabled_features & ROBUSTO_PROXY_FEATURE_PUBSUB_V1) != 0U)
        {
            response.pubsub_major = 1U;
            response.pubsub_minor = 1U;
        }
        response.selected_max_payload = service->session.negotiated_limits.request_pool_bytes;
        response.selected_max_in_flight = service->session.negotiated_limits.max_in_flight;
        response.max_subscriptions = service->session.negotiated_limits.max_subscriptions;

        prefix.status = ROBUSTO_PROXY_STATUS_OK;
        if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK)
        {
            return false;
        }
        if (robusto_proxy_encode_capability_response(
                response_buffer + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                &response) != ROBUSTO_PROXY_RESULT_OK)
        {
            return false;
        }

        service->requests += 1U;
        *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_CAPABILITY_RESPONSE_SIZE_BYTES;
        return true;
    }

    if (request_payload != NULL && request_payload_size > 0U)
    {
        (void)request_payload;
    }

    if (response_buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES)
    {
        return false;
    }

    prefix.status = ROBUSTO_PROXY_STATUS_UNSUPPORTED_OPCODE;
    if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) != ROBUSTO_PROXY_RESULT_OK)
    {
        return false;
    }

    service->errors += 1U;
    *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;
    return true;
}

static bool encode_pubsub_result(
    robusto_proxy_service_t *service,
    uint16_t status,
    uint8_t *response_buffer,
    size_t response_buffer_size,
    size_t success_size,
    size_t *response_size)
{
    robusto_proxy_response_prefix_t prefix;

    if (response_buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES +
                                   (status == ROBUSTO_PROXY_STATUS_OK ? success_size : 0U))
    {
        return false;
    }
    memset(&prefix, 0, sizeof(prefix));
    prefix.status = status;
    if (robusto_proxy_encode_response_prefix(response_buffer, response_buffer_size, &prefix) !=
        ROBUSTO_PROXY_RESULT_OK)
    {
        return false;
    }
    if (status != ROBUSTO_PROXY_STATUS_OK)
    {
        service->errors += 1U;
    }
    *response_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES +
                     (status == ROBUSTO_PROXY_STATUS_OK ? success_size : 0U);
    return true;
}

bool robusto_proxy_service_handle_pubsub_request(
    robusto_proxy_service_t *service,
    uint8_t opcode,
    const uint8_t *request_payload,
    size_t request_payload_size,
    uint8_t *response_buffer,
    size_t response_buffer_size,
    size_t *response_size)
{
    uint16_t status = ROBUSTO_PROXY_STATUS_OK;
    size_t success_size = 0U;
    uint8_t *success_buffer;

    if (service == NULL || response_buffer == NULL || response_size == NULL)
    {
        return false;
    }
    service->requests += 1U;
    if (service->session.state != ROBUSTO_PROXY_SESSION_ESTABLISHED)
    {
        return encode_pubsub_result(service, ROBUSTO_PROXY_STATUS_HANDSHAKE_REQUIRED,
                                    response_buffer, response_buffer_size, 0U, response_size);
    }
    if ((service->session.enabled_features & ROBUSTO_PROXY_FEATURE_PUBSUB_V1) == 0U ||
        service->pubsub_adapter == NULL)
    {
        return encode_pubsub_result(service, ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE,
                                    response_buffer, response_buffer_size, 0U, response_size);
    }
    if (opcode >= ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_BEGIN &&
        opcode <= ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_ABORT &&
        (service->session.enabled_features &
         ROBUSTO_PROXY_FEATURE_PUBSUB_CHUNKED_PUBLISH) == 0U)
    {
        return encode_pubsub_result(service, ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE,
                                    response_buffer, response_buffer_size, 0U, response_size);
    }
    if (response_buffer_size < ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES)
    {
        service->requests -= 1U;
        return false;
    }
    success_buffer = response_buffer + ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;

    if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH)
    {
        robusto_proxy_pubsub_publish_request_t request;
        robusto_proxy_pubsub_publish_response_t response;
        robusto_proxy_result_t decode_result;
        memset(&request, 0, sizeof(request));
        decode_result = robusto_proxy_pubsub_decode_publish_request(
            request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            if (request_payload_size >= ROBUSTO_PROXY_PUBSUB_PUBLISH_HEADER_SIZE_BYTES &&
                request.data_length > ROBUSTO_PROXY_PUBSUB_MAX_INLINE_DATA_BYTES)
            {
                status = ROBUSTO_PROXY_STATUS_PUBSUB_PAYLOAD_TOO_LARGE;
            }
            else if (decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT &&
                     request.operation_id != 0U &&
                     !robusto_proxy_pubsub_topic_is_valid(request.topic, request.topic_length))
            {
                status = ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID;
            }
            else
            {
                status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                             ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                             : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
            }
        }
        else if (service->pubsub_adapter->publish == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            memset(&response, 0, sizeof(response));
            status = service->pubsub_adapter->publish(service->pubsub_adapter_context,
                                                       &request, &response);
            if (status == ROBUSTO_PROXY_STATUS_OK &&
                robusto_proxy_pubsub_encode_publish_response(
                    success_buffer,
                    response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                    &response) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }
            success_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_RESPONSE_SIZE_BYTES;
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_BEGIN)
    {
        robusto_proxy_pubsub_publish_begin_request_t request;
        robusto_proxy_result_t decode_result;
        memset(&request, 0, sizeof(request));
        decode_result = robusto_proxy_pubsub_decode_publish_begin_request(
            request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            if (decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT &&
                request.operation_id != 0U &&
                !robusto_proxy_pubsub_topic_is_valid(request.topic, request.topic_length))
            {
                status = ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID;
            }
            else
            {
                status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                             ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                             : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
            }
        }
        else if (service->pubsub_adapter->publish_begin == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            status = service->pubsub_adapter->publish_begin(
                service->pubsub_adapter_context, &request);
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_CHUNK)
    {
        robusto_proxy_pubsub_publish_chunk_request_t request;
        robusto_proxy_result_t decode_result;
        memset(&request, 0, sizeof(request));
        decode_result = robusto_proxy_pubsub_decode_publish_chunk_request(
            request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                         ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                         : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (service->pubsub_adapter->publish_chunk == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            status = service->pubsub_adapter->publish_chunk(
                service->pubsub_adapter_context, &request);
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_COMMIT)
    {
        robusto_proxy_pubsub_publish_transfer_request_t request;
        robusto_proxy_pubsub_publish_response_t response;
        robusto_proxy_result_t decode_result =
            robusto_proxy_pubsub_decode_publish_transfer_request(
                request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                         ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                         : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (service->pubsub_adapter->publish_commit == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            memset(&response, 0, sizeof(response));
            status = service->pubsub_adapter->publish_commit(
                service->pubsub_adapter_context, &request, &response);
            if (status == ROBUSTO_PROXY_STATUS_OK &&
                robusto_proxy_pubsub_encode_publish_response(
                    success_buffer,
                    response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                    &response) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }
            success_size = ROBUSTO_PROXY_PUBSUB_PUBLISH_RESPONSE_SIZE_BYTES;
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_PUBLISH_ABORT)
    {
        robusto_proxy_pubsub_publish_transfer_request_t request;
        robusto_proxy_result_t decode_result =
            robusto_proxy_pubsub_decode_publish_transfer_request(
                request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                         ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                         : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (service->pubsub_adapter->publish_abort == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            status = service->pubsub_adapter->publish_abort(
                service->pubsub_adapter_context, &request);
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_SUBSCRIBE)
    {
        robusto_proxy_pubsub_subscribe_request_t request;
        robusto_proxy_pubsub_subscribe_response_t response;
        robusto_proxy_result_t decode_result;
        memset(&request, 0, sizeof(request));
        decode_result = robusto_proxy_pubsub_decode_subscribe_request(
            request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            if (decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT &&
                request.operation_id != 0U &&
                !robusto_proxy_pubsub_topic_is_valid(request.topic, request.topic_length))
            {
                status = ROBUSTO_PROXY_STATUS_PUBSUB_TOPIC_INVALID;
            }
            else
            {
                status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                             ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                             : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
            }
        }
        else if (service->pubsub_adapter->subscribe == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            memset(&response, 0, sizeof(response));
            status = service->pubsub_adapter->subscribe(service->pubsub_adapter_context,
                                                         &request, &response);
            if (status == ROBUSTO_PROXY_STATUS_OK &&
                robusto_proxy_pubsub_encode_subscribe_response(
                    success_buffer,
                    response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                    &response) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }
            success_size = ROBUSTO_PROXY_PUBSUB_SUBSCRIBE_RESPONSE_SIZE_BYTES;
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_UNSUBSCRIBE)
    {
        robusto_proxy_pubsub_unsubscribe_request_t request;
        robusto_proxy_pubsub_unsubscribe_response_t response;
        robusto_proxy_result_t decode_result = robusto_proxy_pubsub_decode_unsubscribe_request(
            request_payload, request_payload_size, &request);
        if (decode_result != ROBUSTO_PROXY_RESULT_OK)
        {
            status = decode_result == ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT
                         ? ROBUSTO_PROXY_STATUS_INVALID_ARGUMENT
                         : ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (service->pubsub_adapter->unsubscribe == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            memset(&response, 0, sizeof(response));
            status = service->pubsub_adapter->unsubscribe(service->pubsub_adapter_context,
                                                           &request, &response);
            if (status == ROBUSTO_PROXY_STATUS_OK &&
                robusto_proxy_pubsub_encode_unsubscribe_response(
                    success_buffer,
                    response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                    &response) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }
            success_size = ROBUSTO_PROXY_PUBSUB_UNSUBSCRIBE_RESPONSE_SIZE_BYTES;
        }
    }
    else if (opcode == ROBUSTO_PROXY_PUBSUB_OPCODE_STATUS)
    {
        robusto_proxy_pubsub_status_response_t response;
        if (request_payload_size != 0U)
        {
            status = ROBUSTO_PROXY_STATUS_MALFORMED_PAYLOAD;
        }
        else if (service->pubsub_adapter->status == NULL)
        {
            status = ROBUSTO_PROXY_STATUS_CAPABILITY_UNAVAILABLE;
        }
        else
        {
            (void)request_payload;
            memset(&response, 0, sizeof(response));
            status = service->pubsub_adapter->status(service->pubsub_adapter_context, &response);
            if (status == ROBUSTO_PROXY_STATUS_OK &&
                robusto_proxy_pubsub_encode_status_response(
                    success_buffer,
                    response_buffer_size - ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES,
                    &response) != ROBUSTO_PROXY_RESULT_OK)
            {
                service->requests -= 1U;
                return false;
            }
            success_size = ROBUSTO_PROXY_PUBSUB_STATUS_RESPONSE_SIZE_BYTES;
        }
    }
    else
    {
        status = ROBUSTO_PROXY_STATUS_UNSUPPORTED_OPCODE;
    }

    return encode_pubsub_result(service, status, response_buffer, response_buffer_size,
                                success_size, response_size);
}

robusto_proxy_result_t robusto_proxy_service_build_pubsub_delivery_event(
    robusto_proxy_service_t *service,
    const uint8_t *delivery_payload,
    size_t delivery_payload_size,
    uint8_t *event_frame,
    size_t event_frame_size,
    size_t *encoded_size)
{
    robusto_proxy_frame_header_t header;
    robusto_proxy_pubsub_delivery_t delivery;
    robusto_proxy_result_t result;

    if (service == NULL || delivery_payload == NULL || event_frame == NULL ||
        encoded_size == NULL ||
        service->session.state != ROBUSTO_PROXY_SESSION_ESTABLISHED ||
        (service->session.enabled_features & ROBUSTO_PROXY_FEATURE_PUBSUB_V1) == 0U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    result = robusto_proxy_pubsub_decode_delivery(delivery_payload,
                                                   delivery_payload_size,
                                                   &delivery);
    if (result != ROBUSTO_PROXY_RESULT_OK)
    {
        return result;
    }
    robusto_proxy_frame_header_init(
        &header,
        ROBUSTO_PROXY_FLAG_EVENT,
        ROBUSTO_PROXY_DOMAIN_PUBSUB,
        ROBUSTO_PROXY_PUBSUB_OPCODE_DELIVERY,
        0U,
        robusto_proxy_session_take_sequence(&service->session),
        (uint32_t)delivery_payload_size);
    result = robusto_proxy_frame_encode(event_frame, event_frame_size, &header,
                                        delivery_payload, encoded_size);
    if (result == ROBUSTO_PROXY_RESULT_OK)
    {
        service->events += 1U;
    }
    return result;
}

robusto_proxy_result_t robusto_proxy_service_handle_frame(
    robusto_proxy_service_t *service,
    const uint8_t *request_frame,
    size_t request_frame_size,
    uint32_t now_ms,
    uint8_t *response_frame,
    size_t response_frame_size,
    size_t *response_size)
{
    const robusto_proxy_frame_header_t *request_header;
    robusto_proxy_frame_header_t response_header;
    uint8_t response_payload[ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES + ROBUSTO_PROXY_HEALTH_RESPONSE_SIZE_BYTES];
    size_t response_payload_size = 0U;
    robusto_proxy_result_t result;

    if (service == NULL || request_frame == NULL || response_frame == NULL || response_size == NULL)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    result = robusto_proxy_frame_validate_buffer(request_frame, request_frame_size, NULL);
    if (result != ROBUSTO_PROXY_RESULT_OK)
    {
        return result;
    }

    request_header = (const robusto_proxy_frame_header_t *)request_frame;
    if ((request_header->flags & ROBUSTO_PROXY_FLAG_REQUEST) == 0U ||
        request_header->correlation_id == 0U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    if (request_header->domain == ROBUSTO_PROXY_DOMAIN_CONTROL)
    {
        result = robusto_proxy_service_handle_control_request(
            service, request_header->opcode,
            request_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
            request_header->payload_length, now_ms, response_payload,
            sizeof(response_payload), &response_payload_size)
                     ? ROBUSTO_PROXY_RESULT_OK
                     : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    else if (request_header->domain == ROBUSTO_PROXY_DOMAIN_PUBSUB)
    {
        result = robusto_proxy_service_handle_pubsub_request(
            service, request_header->opcode,
            request_frame + ROBUSTO_PROXY_HEADER_SIZE_BYTES,
            request_header->payload_length, response_payload,
            sizeof(response_payload), &response_payload_size)
                     ? ROBUSTO_PROXY_RESULT_OK
                     : ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }
    else
    {
        robusto_proxy_response_prefix_t prefix;
        memset(&prefix, 0, sizeof(prefix));
        prefix.status = ROBUSTO_PROXY_STATUS_UNSUPPORTED_DOMAIN;
        result = robusto_proxy_encode_response_prefix(response_payload,
                                                       sizeof(response_payload),
                                                       &prefix);
        response_payload_size = ROBUSTO_PROXY_RESPONSE_PREFIX_SIZE_BYTES;
        service->errors += 1U;
    }
    if (result != ROBUSTO_PROXY_RESULT_OK)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    robusto_proxy_frame_header_init(
        &response_header,
        ROBUSTO_PROXY_FLAG_RESPONSE,
        request_header->domain,
        request_header->opcode,
        request_header->correlation_id,
        robusto_proxy_session_take_sequence(&service->session),
        (uint32_t)response_payload_size);
    return robusto_proxy_frame_encode(
        response_frame,
        response_frame_size,
        &response_header,
        response_payload,
        response_size);
}
