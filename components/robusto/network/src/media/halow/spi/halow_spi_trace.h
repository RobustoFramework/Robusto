#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SEQUENCE_TRACE_MAX 32U
#define SEQUENCE_BYTES_MAX 69U

typedef struct {
    char operation;
    unsigned int length;
    int64_t begin_us;
    int64_t pre_us;
    int64_t post_us;
    int64_t end_us;
    unsigned int repetitions;
    int64_t last_begin_us;
    int64_t last_pre_us;
    int64_t last_post_us;
    int32_t result;
    uint8_t cs_begin;
    uint8_t cs_pre;
    uint8_t cs_post;
    uint8_t cs_end;
    uint8_t tx[SEQUENCE_BYTES_MAX];
    uint8_t rx[SEQUENCE_BYTES_MAX];
    bool has_rx;
    bool pre_captured;
    bool post_captured;
    uint32_t spi_user;
    uint32_t spi_misc;
    unsigned int spi_bits;
} sequence_trace_t;

static inline bool robusto_halow_spi_compact_read(sequence_trace_t *previous,
                                               const sequence_trace_t *entry)
{
    if (entry->operation != 'R' || previous->operation != 'R' ||
        entry->length > SEQUENCE_BYTES_MAX || previous->length != entry->length ||
        entry->result != 0 || previous->result != 0 ||
        !entry->has_rx || !previous->has_rx ||
        !entry->pre_captured || !entry->post_captured ||
        !previous->pre_captured || !previous->post_captured ||
        previous->spi_user != entry->spi_user || previous->spi_misc != entry->spi_misc ||
        previous->spi_bits != entry->spi_bits ||
        previous->cs_begin != entry->cs_begin || previous->cs_end != entry->cs_end ||
        previous->cs_pre != entry->cs_pre || previous->cs_post != entry->cs_post ||
        memcmp(previous->tx, entry->tx, entry->length) != 0 ||
        memcmp(previous->rx, entry->rx, entry->length) != 0) {
        return false;
    }
    previous->repetitions += entry->repetitions;
    previous->end_us = entry->end_us;
    previous->last_begin_us = entry->begin_us;
    previous->last_pre_us = entry->pre_us;
    previous->last_post_us = entry->post_us;
    return true;
}
