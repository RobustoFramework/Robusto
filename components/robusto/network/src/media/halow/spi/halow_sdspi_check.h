#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ROBUSTO_SDSPI_BLOCK_BYTES 64U
#define ROBUSTO_SDSPI_CHECK_BLOCKS 26U

enum {
    ROBUSTO_SDSPI_CHECK_COMMAND = 1U,
    ROBUSTO_SDSPI_CHECK_BLOCK_COUNT = 2U,
    ROBUSTO_SDSPI_CHECK_PAYLOAD = 4U,
    ROBUSTO_SDSPI_CHECK_TOKEN = 8U,
    ROBUSTO_SDSPI_CHECK_BUSY = 16U,
    ROBUSTO_SDSPI_CHECK_SPI = 32U,
    ROBUSTO_SDSPI_CHECK_ORDER = 64U,
    ROBUSTO_SDSPI_CHECK_RETURN = 128U,
};

typedef struct {
    const uint8_t *data;
    uint32_t bytes, expected_blocks, declared_blocks, blocks, commands, stops;
    uint32_t flags, first_bad_block, busy_reads;
    uint8_t response[ROBUSTO_SDSPI_CHECK_BLOCKS];
    uint8_t busy[ROBUSTO_SDSPI_CHECK_BLOCKS];
    bool active, busy_pending;
} robusto_sdspi_check_t;

static inline void robusto_halow_sdspi_check_begin(robusto_sdspi_check_t *check,
                                                  const uint8_t *data, uint32_t bytes)
{
    memset(check, 0, sizeof(*check));
    check->data = data;
    check->bytes = bytes;
    check->expected_blocks = (bytes + ROBUSTO_SDSPI_BLOCK_BYTES - 1U) /
                             ROBUSTO_SDSPI_BLOCK_BYTES;
    check->first_bad_block = UINT32_MAX;
    check->active = true;
}

static inline void robusto_halow_sdspi_check_bad_block(robusto_sdspi_check_t *check,
                                                       uint32_t index, uint32_t flags)
{
    check->flags |= flags;
    if (check->first_bad_block == UINT32_MAX) check->first_bad_block = index;
}

static inline void robusto_halow_sdspi_check_transfer(robusto_sdspi_check_t *check,
    const uint8_t *tx, const uint8_t *rx, unsigned int length, bool spi_ok)
{
    if (!check->active) return;
    if (!spi_ok) {
        check->flags |= ROBUSTO_SDSPI_CHECK_SPI;
        return;
    }
    if (tx != NULL && rx != NULL && length == 17U &&
        tx[0] == 0xff && tx[1] == 0x75 && (tx[2] & 0x80U)) {
        if (check->blocks != 0 || check->commands != 0 || check->stops != 0)
            check->flags |= ROBUSTO_SDSPI_CHECK_ORDER;
        ++check->commands;
        check->declared_blocks = (((uint32_t)tx[4] << 8) | tx[5]) & 0x1ffU;
        if (check->declared_blocks == 0) check->declared_blocks = 512U;
        if (!(tx[2] & 0x08U) || check->declared_blocks != check->expected_blocks)
            check->flags |= ROBUSTO_SDSPI_CHECK_COMMAND;
    } else if (tx != NULL && rx != NULL && length == 69U && tx[0] == 0xfc) {
        uint32_t index = check->blocks;
        if (check->commands != 1 || check->stops != 0)
            check->flags |= ROBUSTO_SDSPI_CHECK_ORDER;
        if (check->busy_pending)
            robusto_halow_sdspi_check_bad_block(check, index == 0 ? 0 : index - 1U,
                                               ROBUSTO_SDSPI_CHECK_BUSY);
        if (index >= check->expected_blocks || index >= ROBUSTO_SDSPI_CHECK_BLOCKS) {
            robusto_halow_sdspi_check_bad_block(check, index, ROBUSTO_SDSPI_CHECK_BLOCK_COUNT);
        } else {
            uint32_t offset = index * ROBUSTO_SDSPI_BLOCK_BYTES;
            uint32_t count = check->bytes - offset;
            if (count > ROBUSTO_SDSPI_BLOCK_BYTES) count = ROBUSTO_SDSPI_BLOCK_BYTES;
            if (memcmp(tx + 1, check->data + offset, count) != 0)
                robusto_halow_sdspi_check_bad_block(check, index, ROBUSTO_SDSPI_CHECK_PAYLOAD);
            check->response[index] = rx[67];
            check->busy[index] = rx[68];
        }
        /* SD SPI data-response tokens use the low five bits; 0xe5 is also accepted. */
        if ((rx[67] & 0x1fU) != 0x05U)
            robusto_halow_sdspi_check_bad_block(check, index, ROBUSTO_SDSPI_CHECK_TOKEN);
        check->busy_pending = rx[68] != 0xffU;
        ++check->blocks;
    } else if (tx != NULL && length == 1U && tx[0] == 0xfd) {
        if (check->busy_pending)
            robusto_halow_sdspi_check_bad_block(check,
                check->blocks == 0 ? 0 : check->blocks - 1U, ROBUSTO_SDSPI_CHECK_BUSY);
        if (check->blocks != check->expected_blocks || check->stops != 0)
            check->flags |= ROBUSTO_SDSPI_CHECK_ORDER;
        ++check->stops;
        check->busy_pending = true;
    } else if (tx == NULL && rx != NULL && check->busy_pending) {
        ++check->busy_reads;
        for (unsigned int i = 0; i < length; ++i) {
            if (rx[i] != 0) {
                check->busy_pending = false;
                break;
            }
        }
    }
}

static inline bool robusto_halow_sdspi_check_finish(robusto_sdspi_check_t *check, int sent)
{
    if (check->commands != 1 || check->declared_blocks != check->expected_blocks)
        check->flags |= ROBUSTO_SDSPI_CHECK_COMMAND;
    if (check->blocks != check->expected_blocks)
        check->flags |= ROBUSTO_SDSPI_CHECK_BLOCK_COUNT;
    if (check->stops != 1) check->flags |= ROBUSTO_SDSPI_CHECK_ORDER;
    if (check->busy_pending) check->flags |= ROBUSTO_SDSPI_CHECK_BUSY;
    if (sent != (int)check->bytes) check->flags |= ROBUSTO_SDSPI_CHECK_RETURN;
    check->active = false;
    return check->flags == 0;
}
