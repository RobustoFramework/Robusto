#include "robusto_proxy_inflight.h"

#include <string.h>

static uint16_t clamp_capacity(uint16_t negotiated_capacity)
{
    if (negotiated_capacity == 0U)
    {
        return 1U;
    }

    if (negotiated_capacity > ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS)
    {
        return ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS;
    }

    return negotiated_capacity;
}

void robusto_proxy_inflight_init(
    robusto_proxy_inflight_table_t *table,
    uint16_t negotiated_capacity)
{
    if (table == NULL)
    {
        return;
    }

    memset(table, 0, sizeof(*table));
    table->capacity = clamp_capacity(negotiated_capacity);
}

robusto_proxy_result_t robusto_proxy_inflight_begin(
    robusto_proxy_inflight_table_t *table,
    uint8_t domain,
    uint8_t opcode,
    uint32_t correlation_id,
    uint32_t sequence,
    uint32_t started_at_ms,
    uint32_t timeout_ms,
    uint64_t peer_boot_id)
{
    size_t index;

    if (table == NULL || correlation_id == 0U || timeout_ms == 0U)
    {
        return ROBUSTO_PROXY_RESULT_INVALID_ARGUMENT;
    }

    if (table->active_count >= table->capacity)
    {
        return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
    }

    if (robusto_proxy_inflight_find(table, correlation_id) != NULL)
    {
        return ROBUSTO_PROXY_RESULT_BAD_RESERVED;
    }

    for (index = 0; index < ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS; ++index)
    {
        robusto_proxy_inflight_entry_t *entry = &table->entries[index];
        if (entry->status == ROBUSTO_PROXY_INFLIGHT_STATUS_FREE ||
            entry->status == ROBUSTO_PROXY_INFLIGHT_STATUS_COMPLETED ||
            entry->status == ROBUSTO_PROXY_INFLIGHT_STATUS_STALE_SESSION)
        {
            entry->status = ROBUSTO_PROXY_INFLIGHT_STATUS_ACTIVE;
            entry->domain = domain;
            entry->opcode = opcode;
            entry->correlation_id = correlation_id;
            entry->sequence = sequence;
            entry->started_at_ms = started_at_ms;
            entry->timeout_ms = timeout_ms;
            entry->peer_boot_id = peer_boot_id;
            ++table->active_count;
            return ROBUSTO_PROXY_RESULT_OK;
        }
    }

    return ROBUSTO_PROXY_RESULT_BAD_LENGTH;
}

robusto_proxy_inflight_entry_t *robusto_proxy_inflight_find(
    robusto_proxy_inflight_table_t *table,
    uint32_t correlation_id)
{
    size_t index;

    if (table == NULL || correlation_id == 0U)
    {
        return NULL;
    }

    for (index = 0; index < ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS; ++index)
    {
        robusto_proxy_inflight_entry_t *entry = &table->entries[index];
        if (entry->status == ROBUSTO_PROXY_INFLIGHT_STATUS_ACTIVE &&
            entry->correlation_id == correlation_id)
        {
            return entry;
        }
    }

    return NULL;
}

bool robusto_proxy_inflight_complete(
    robusto_proxy_inflight_table_t *table,
    uint32_t correlation_id)
{
    robusto_proxy_inflight_entry_t *entry = robusto_proxy_inflight_find(table, correlation_id);

    if (entry == NULL)
    {
        return false;
    }

    entry->status = ROBUSTO_PROXY_INFLIGHT_STATUS_COMPLETED;
    entry->correlation_id = 0U;
    if (table->active_count > 0U)
    {
        --table->active_count;
    }

    return true;
}

bool robusto_proxy_inflight_is_expired(
    const robusto_proxy_inflight_entry_t *entry,
    uint32_t now_ms)
{
    uint32_t elapsed;

    if (entry == NULL || entry->status != ROBUSTO_PROXY_INFLIGHT_STATUS_ACTIVE)
    {
        return false;
    }

    elapsed = now_ms - entry->started_at_ms;
    return elapsed >= entry->timeout_ms;
}

uint16_t robusto_proxy_inflight_expire(
    robusto_proxy_inflight_table_t *table,
    uint32_t now_ms)
{
    uint16_t expired = 0U;
    size_t index;

    if (table == NULL)
    {
        return 0U;
    }

    for (index = 0; index < ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS; ++index)
    {
        robusto_proxy_inflight_entry_t *entry = &table->entries[index];
        if (robusto_proxy_inflight_is_expired(entry, now_ms))
        {
            entry->status = ROBUSTO_PROXY_INFLIGHT_STATUS_COMPLETED;
            entry->correlation_id = 0U;
            ++expired;
        }
    }

    if (expired > table->active_count)
    {
        table->active_count = 0U;
    }
    else
    {
        table->active_count = (uint16_t)(table->active_count - expired);
    }

    return expired;
}

uint16_t robusto_proxy_inflight_invalidate_peer(
    robusto_proxy_inflight_table_t *table,
    uint64_t peer_boot_id)
{
    uint16_t invalidated = 0U;
    size_t index;

    if (table == NULL)
    {
        return 0U;
    }

    for (index = 0; index < ROBUSTO_PROXY_MAX_INFLIGHT_REQUESTS; ++index)
    {
        robusto_proxy_inflight_entry_t *entry = &table->entries[index];
        if (entry->status == ROBUSTO_PROXY_INFLIGHT_STATUS_ACTIVE &&
            entry->peer_boot_id == peer_boot_id)
        {
            entry->status = ROBUSTO_PROXY_INFLIGHT_STATUS_STALE_SESSION;
            entry->correlation_id = 0U;
            ++invalidated;
        }
    }

    if (invalidated > table->active_count)
    {
        table->active_count = 0U;
    }
    else
    {
        table->active_count = (uint16_t)(table->active_count - invalidated);
    }

    return invalidated;
}
