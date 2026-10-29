#include "halow_sdspi_check.h"
#include "hgic_sdspi.h"
#include <assert.h>
#include <stdio.h>

static robusto_sdspi_check_t check;
static uint8_t data[1664];
static unsigned int blocks, stops;
static uint8_t token;
static bool stuck_block, stuck_stop, corrupt_payload, wrong_count, skip_block, skip_stop;

void spidrv_write_read(void *priv, unsigned char *tx, unsigned char *rx, unsigned int length)
{
    (void)priv;
    uint8_t submitted[69];
    assert(length <= sizeof(submitted));
    memcpy(submitted, tx, length);
    memset(rx, 0xff, length);
    if (length == 17 && submitted[0] == 0xff && submitted[1] == 0x75) {
        rx[8] = 0;
        if (wrong_count) ++submitted[5];
    } else {
        assert(length == 69 && submitted[0] == 0xfc);
        ++blocks;
        rx[67] = token;
        rx[68] = 0;
        if (corrupt_payload && blocks == 2) submitted[1] ^= 1;
        if (skip_block && blocks == 2) return;
    }
    robusto_halow_sdspi_check_transfer(&check, submitted, rx, length, true);
}

void spidrv_write(void *priv, unsigned char *bytes, unsigned int length, char dma)
{
    (void)priv; (void)dma;
    assert(length == 1 && bytes[0] == 0xfd);
    ++stops;
    if (!skip_stop) robusto_halow_sdspi_check_transfer(&check, bytes, NULL, length, true);
}

void spidrv_read(void *priv, unsigned char *bytes, unsigned int length, char dma)
{
    (void)priv; (void)dma;
    assert(length == 29);
    bool stuck = stops != 0 ? stuck_stop : (stuck_block && blocks == 2);
    memset(bytes, stuck ? 0 : 0xff, length);
    robusto_halow_sdspi_check_transfer(&check, NULL, bytes, length, true);
}

void spidrv_cs(void *priv, char enable) { (void)priv; (void)enable; }
int spidrv_hw_crc(void *priv, unsigned char *bytes, unsigned int length, char flag)
{ (void)priv; (void)bytes; (void)length; (void)flag; return 0; }

static int write_fixture(uint32_t bytes)
{
    blocks = stops = 0;
    robusto_halow_sdspi_check_begin(&check, data, bytes);
    int sent = hgic_sdspi_write(NULL, data, bytes);
    assert(sent == (int)bytes);
    return sent;
}

int main(void)
{
    for (unsigned int i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 37U + 11U);
    const uint32_t raw_lengths[] = {1, 256, 384, 448, 490, 491, 512, 1024, 1642};
    token = 0xe5;
    for (unsigned int i = 0; i < sizeof(raw_lengths) / sizeof(raw_lengths[0]); ++i) {
        uint32_t bytes = raw_lengths[i] + 22U;
        assert(robusto_halow_sdspi_check_finish(&check, write_fixture(bytes)));
        assert(check.blocks == (bytes + 63U) / 64U);
        assert(check.flags == 0 && !check.busy_pending && stops == 1);
    }
    token = 0x05;
    assert(robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    const uint8_t bad_tokens[] = {0xeb, 0xed, 0xff, 0x00};
    for (unsigned int i = 0; i < sizeof(bad_tokens); ++i) {
        token = bad_tokens[i];
        assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
        assert(check.flags & ROBUSTO_SDSPI_CHECK_TOKEN);
        assert(check.first_bad_block == 0 && check.response[0] == token);
    }
    token = 0xe5;
    stuck_block = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_BUSY);
    assert(check.first_bad_block == 1 && check.busy_reads >= 2048);
    stuck_block = false;
    stuck_stop = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_BUSY);
    stuck_stop = false;
    corrupt_payload = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_PAYLOAD);
    assert(check.first_bad_block == 1);
    corrupt_payload = false;
    wrong_count = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_COMMAND);
    wrong_count = false;
    skip_block = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_BLOCK_COUNT);
    skip_block = false;
    skip_stop = true;
    assert(!robusto_halow_sdspi_check_finish(&check, write_fixture(534)));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_ORDER);
    skip_stop = false;
    int sent = write_fixture(534);
    assert(!robusto_halow_sdspi_check_finish(&check, sent - 1));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_RETURN);
    robusto_halow_sdspi_check_begin(&check, data, 534);
    robusto_halow_sdspi_check_transfer(&check, NULL, NULL, 69, false);
    assert(!robusto_halow_sdspi_check_finish(&check, -1));
    assert(check.flags & ROBUSTO_SDSPI_CHECK_SPI);
    puts("SDSPI checks: real vendor multi-block writes at raw 1/256/384/448/490/491/512/1024/1642; accepted/rejected tokens, ignored busy timeouts, payload/count/stop/SPI/return failures passed.");
    return 0;
}
