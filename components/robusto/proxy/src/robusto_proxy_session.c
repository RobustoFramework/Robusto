#include "robusto_proxy_session.h"

static uint32_t nonzero_u32(uint32_t value)
{
    return value == 0U ? 1U : value;
}

void robusto_proxy_session_init(
    robusto_proxy_session_t *session,
    robusto_proxy_profile_t profile,
    uint64_t local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed)
{
    session->state = ROBUSTO_PROXY_SESSION_RESET;
    session->local_profile = profile;
    session->local_limits = robusto_proxy_profile_limits(profile);
    session->negotiated_limits = session->local_limits;
    session->local_boot_id = local_boot_id;
    session->peer_boot_id = 0U;
    session->enabled_features = 0U;
    session->selected_protocol_major = 0U;
    session->selected_protocol_minor = 0U;
    session->timeout_events = 0U;
    session->next_correlation_id = nonzero_u32(correlation_seed);
    session->next_sequence = nonzero_u32(sequence_seed);
}

void robusto_proxy_session_reset(
    robusto_proxy_session_t *session,
    uint64_t new_local_boot_id,
    uint32_t correlation_seed,
    uint32_t sequence_seed)
{
    robusto_proxy_session_init(
        session,
        session->local_profile,
        new_local_boot_id,
        correlation_seed,
        sequence_seed);
}

uint32_t robusto_proxy_session_take_correlation_id(robusto_proxy_session_t *session)
{
    uint32_t value = session->next_correlation_id;
    session->next_correlation_id = nonzero_u32(value + 1U);
    return value;
}

uint32_t robusto_proxy_session_take_sequence(robusto_proxy_session_t *session)
{
    uint32_t value = session->next_sequence;
    session->next_sequence = nonzero_u32(value + 1U);
    return value;
}

bool robusto_proxy_session_apply_hello_response(
    robusto_proxy_session_t *session,
    const robusto_proxy_hello_response_t *response)
{
    if (response == NULL)
    {
        return false;
    }

    if (response->selected_protocol_major != ROBUSTO_PROXY_PROTOCOL_MAJOR)
    {
        session->state = ROBUSTO_PROXY_SESSION_INCOMPATIBLE;
        return false;
    }

    if (response->selected_max_payload > session->local_limits.request_pool_bytes)
    {
        session->state = ROBUSTO_PROXY_SESSION_INCOMPATIBLE;
        return false;
    }

    session->peer_boot_id = response->proxy_boot_id;
    session->enabled_features = response->enabled_features;
    session->selected_protocol_major = response->selected_protocol_major;
    session->selected_protocol_minor = response->selected_protocol_minor;
    session->negotiated_limits.max_in_flight = response->selected_max_in_flight;
    session->negotiated_limits.max_subscriptions = session->local_limits.max_subscriptions;
    session->negotiated_limits.dedupe_entries = response->dedupe_entries;
    session->negotiated_limits.dedupe_window_ms = response->dedupe_window_ms;
    session->negotiated_limits.request_pool_bytes = response->selected_max_payload;
    session->negotiated_limits.response_pool_bytes = response->selected_max_payload;
    session->state = ROBUSTO_PROXY_SESSION_ESTABLISHED;

    return true;
}


bool robusto_proxy_session_note_expired_requests(
    robusto_proxy_session_t *session,
    uint16_t expired_count)
{
    if (session == NULL || expired_count == 0U)
    {
        return false;
    }

    session->timeout_events += expired_count;
    if (session->state == ROBUSTO_PROXY_SESSION_ESTABLISHED)
    {
        session->state = ROBUSTO_PROXY_SESSION_DEGRADED;
    }

    return true;
}

bool robusto_proxy_session_apply_health_metrics(
    const robusto_proxy_session_t *session,
    robusto_proxy_health_response_t *response)
{
    if (session == NULL || response == NULL)
    {
        return false;
    }

    if (session->state == ROBUSTO_PROXY_SESSION_ESTABLISHED)
    {
        response->service_state = 1U;
    }
    else if (session->state == ROBUSTO_PROXY_SESSION_DEGRADED)
    {
        response->service_state = 2U;
    }
    else
    {
        response->service_state = 0U;
    }

    response->timeouts = session->timeout_events;
    return true;
}
