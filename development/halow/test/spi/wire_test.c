#include "halow_spi_wire.h"
#include "halow_spi_trace.h"
#include "hgic_raw.h"
#include "hgic_sdspi.h"
#include <string.h>

extern struct hgic_raw hgic;

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

/* Freestanding test runtime; the production parser never calls the vendor's
 * unbounded command/event text formatter. Link the actual vendor raw source. */
void *memset(void *destination, int value, size_t length)
{
    unsigned char *data = destination;
    for (size_t i = 0; i < length; ++i) data[i] = value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}

static unsigned int cookie_warnings;
int printf(const char *format, ...)
{
    if (strncmp(format, "cookie err:", 11) == 0) ++cookie_warnings;
    return 0;
}
int sprintf(char *data, const char *format, ...) { (void)data; (void)format; return 0; }
static unsigned int submitted_length;
static bool test_sdspi;
static unsigned char submitted_block[64];
static unsigned int block_count;
static unsigned int cmd53_count;
static unsigned int stop_count;
static unsigned int cs_assert_count;
static unsigned int cs_release_count;
/* These track vendor operation markers, not the peripheral's physical CS. */
static bool cs_active;
static bool unexpected_transfer;
static char transfer_sequence[16];
static unsigned int transfer_sequence_length;
static unsigned int busy_read_count;

static void record_transfer(char operation)
{
    if (transfer_sequence_length < sizeof(transfer_sequence)) {
        transfer_sequence[transfer_sequence_length++] = operation;
    } else {
        unexpected_transfer = true;
    }
}
int raw_send(unsigned char *data, unsigned int length)
{
    submitted_length = length;
    return test_sdspi ? hgic_sdspi_write(NULL, data, length) : (int)length;
}

void spidrv_write_read(void *priv, unsigned char *tx, unsigned char *rx,
                      unsigned int length)
{
    (void)priv;
    if (!cs_active) {
        unexpected_transfer = true;
    }
    if (length == 17 && tx[0] == 0xff && tx[1] == 0x75 &&
        tx[2] == 0x9c && tx[3] == 0 && tx[4] == 0 && tx[5] == 1) {
        ++cmd53_count;
        record_transfer('C');
        memset(rx, 0xff, length);
        rx[8] = 0;
    } else if (length == 69 && tx[0] == 0xfc) {
        ++block_count;
        record_transfer('B');
        memcpy(submitted_block, tx + 1, sizeof(submitted_block));
        memset(rx, 0xff, length);
        rx[67] = 0xe5; /* Accepted data token, as captured on the board. */
        rx[68] = 0; /* Busy after the accepted token. */
    } else {
        unexpected_transfer = true;
        memset(rx, 0xff, length);
    }
}

void spidrv_write(void *priv, unsigned char *data, unsigned int length, char dma)
{
    (void)priv; (void)dma;
    if (!cs_active) {
        unexpected_transfer = true;
    }
    if (length == 1 && data[0] == 0xfd) {
        ++stop_count;
        record_transfer('S');
    } else {
        unexpected_transfer = true;
    }
}

void spidrv_read(void *priv, unsigned char *data, unsigned int length, char dma)
{
    (void)priv; (void)dma;
    if (!cs_active) {
        unexpected_transfer = true;
    }
    record_transfer('R');
    ++busy_read_count;
    if (length != 29) {
        unexpected_transfer = true;
    }
    memset(data, busy_read_count == 1 ? 0 : 0xff, length);
}

void spidrv_cs(void *priv, char enable)
{
    (void)priv;
    if (cs_active == (enable != 0)) {
        unexpected_transfer = true;
    }
    cs_active = enable != 0;
    record_transfer(cs_active ? '+' : '-');
    if (cs_active) {
        ++cs_assert_count;
    } else {
        ++cs_release_count;
    }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int length, char flag)
{
    (void)priv; (void)data; (void)length; (void)flag;
    return 0;
}

static void header(unsigned char *buffer, unsigned char type, unsigned short length)
{
    memset(buffer, 0, ROBUSTO_HALOW_SPI_RX_CAPACITY);
    put_unaligned_le16(HGIC_HDR_RX_MAGIC, buffer);
    buffer[2] = type;
    put_unaligned_le16(length, buffer + 4);
}

int test_main(void)
{
    sequence_trace_t grouped = {
        .operation = 'R', .length = 29, .begin_us = 100, .pre_us = 110,
        .post_us = 120, .end_us = 130, .repetitions = 1,
        .has_rx = true, .pre_captured = true, .post_captured = true, .spi_bits = 232,
    };
    memset(grouped.tx, 0xff, grouped.length);
    sequence_trace_t repeated = grouped;
    for (unsigned int i = 1; i < 80; ++i) {
        repeated.begin_us = 100 + i * 50;
        repeated.pre_us = repeated.begin_us + 10;
        repeated.post_us = repeated.begin_us + 20;
        repeated.end_us = repeated.begin_us + 30;
        CHECK(robusto_halow_spi_compact_read(&grouped, &repeated));
    }
    CHECK(grouped.repetitions == 80);
    CHECK(grouped.begin_us == 100 && grouped.pre_us == 110 && grouped.post_us == 120);
    CHECK(grouped.end_us == 4080 && grouped.last_begin_us == 4050);
    CHECK(grouped.last_pre_us == 4060 && grouped.last_post_us == 4070);
    repeated.rx[28] = 0xff;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.rx[28] = 0;
    repeated.operation = 'W'; /* A stop-token transfer must never be compacted. */
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.operation = '-';
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.operation = 'R';
    repeated.cs_end = 1;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.cs_end = 0;
    repeated.result = -1;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.result = 0;
    repeated.post_captured = false;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.post_captured = true;
    repeated.spi_user = 1;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.spi_user = 0;
    repeated.spi_misc = 1;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    repeated.spi_misc = 0;
    repeated.length = SEQUENCE_BYTES_MAX + 1;
    CHECK(!robusto_halow_spi_compact_read(&grouped, &repeated));
    unsigned char buffer[ROBUSTO_HALOW_SPI_RX_CAPACITY];
    robusto_halow_spi_packet_t packet;
    robusto_halow_spi_reset_rx_cookie();
    header(buffer, HGIC_HDR_TYPE_SOFTFC, sizeof(struct hgic_hdr));
    put_unaligned_le16(7, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, sizeof(struct hgic_hdr), &packet));
    CHECK(packet.flow_control && hgic.rxcookie == 0);
    header(buffer, HGIC_HDR_TYPE_CMD2, CMD_HDR_LEN);
    put_unaligned_le16(8, buffer + 6);
    put_unaligned_le16(HGIC_CMD_SET_WORK_MODE, buffer + 8);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(packet.command && hgic.rxcookie == 0);
    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN);
    put_unaligned_le16(9, buffer + 6);
    buffer[8] = HGIC_EVENT_CONECT_START;
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(packet.event && hgic.rxcookie == 0);
    header(buffer, HGIC_HDR_TYPE_FRM2, 1664);
    buffer[14] = 0x12; /* Ethernet source starts at vendor offset 8+6. */
    buffer[20] = 0x66;
    buffer[21] = 0xaa;
    buffer[22] = 'R';
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(cookie_warnings == 0);
    CHECK(packet.length == 1642);
    CHECK(packet.frame == buffer + 22 && packet.frame[0] == 'R');
    CHECK(packet.source[0] == 0x12);
    CHECK(!packet.cookie_discontinuity && hgic.rxcookie == 0);
    put_unaligned_le16(1, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(!packet.cookie_discontinuity && hgic.rxcookie == 1);
    put_unaligned_le16(3, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(packet.cookie_discontinuity && packet.cookie_previous == 1 &&
          packet.cookie_expected == 2 && packet.cookie_received == 3);
    CHECK(packet.frame == buffer + 22 && packet.length == 1642 &&
          packet.frame[0] == 'R' && packet.source[0] == 0x12);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(packet.cookie_discontinuity && packet.cookie_expected == 4 &&
          packet.cookie_received == 3);
    put_unaligned_le16(2, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(packet.cookie_discontinuity && packet.cookie_previous == 3 &&
          packet.cookie_received == 2);
    robusto_halow_spi_reset_rx_cookie();
    put_unaligned_le16(65535, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(!packet.cookie_discontinuity && hgic.rxcookie == 65535);
    put_unaligned_le16(0, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 1664, &packet));
    CHECK(!packet.cookie_discontinuity && packet.cookie_expected == 0 &&
          hgic.rxcookie == 0);
    /* SDIO block padding must not become payload. */
    CHECK(robusto_halow_spi_parse(buffer, 1728, &packet));
    CHECK(packet.length == 1642);
    CHECK(packet.cookie_discontinuity);
    put_unaligned_le16(100, buffer + 6);
    put_unaligned_le16(1665, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, 1728, &packet));
    put_unaligned_le16(1664, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, 1663, &packet));
    buffer[20] = 0;
    CHECK(!robusto_halow_spi_parse(buffer, 1664, &packet));
    buffer[20] = 0x66;
    buffer[0] = 0;
    CHECK(!robusto_halow_spi_parse(buffer, 1664, &packet));
    header(buffer, HGIC_HDR_TYPE_FRM2, 22);
    CHECK(!robusto_halow_spi_parse(buffer, 22, &packet));
    for (unsigned i = 0; i < sizeof(struct hgic_hdr); ++i) {
        CHECK(!robusto_halow_spi_parse(buffer, i, &packet));
    }
    CHECK(hgic.rxcookie == 0);
    header(buffer, HGIC_HDR_TYPE_TEST2, 32);
    CHECK(robusto_halow_spi_parse(buffer, 32, &packet));
    CHECK(packet.loopback && !packet.loopback_valid && packet.length == 0);
    header(buffer, HGIC_HDR_TYPE_TEST2, sizeof(struct hgic_hdr) + 4);
    memcpy(buffer + sizeof(struct hgic_hdr), ROBUSTO_HALOW_SPI_AP_PROBE, 4);
    CHECK(robusto_halow_spi_parse(buffer, sizeof(struct hgic_hdr) + 4, &packet));
    CHECK(packet.loopback && packet.loopback_valid && packet.frame == NULL);
    CHECK(packet.loopback_length == 4 &&
          packet.loopback_payload == buffer + sizeof(struct hgic_hdr));
    CHECK(!robusto_halow_spi_parse(buffer, sizeof(struct hgic_hdr) + 3, &packet));
    const unsigned int echo_sizes[] = {278, 406, 470, 512, 513, 534, 1046};
    for (unsigned int i = 0; i < sizeof(echo_sizes) / sizeof(echo_sizes[0]); ++i) {
        unsigned int total = echo_sizes[i];
        header(buffer, HGIC_HDR_TYPE_TEST2, total);
        memset(buffer + sizeof(struct hgic_hdr), 0x5a, total - sizeof(struct hgic_hdr));
        CHECK(robusto_halow_spi_parse(buffer, (total + 63U) & ~63U, &packet));
        CHECK(packet.loopback && !packet.loopback_valid && packet.frame == NULL);
        CHECK(packet.length == 0 && packet.loopback_length == total - sizeof(struct hgic_hdr));
        CHECK(packet.loopback_payload[packet.loopback_length - 1] == 0x5a);
        CHECK(!robusto_halow_spi_parse(buffer, total - 1U, &packet));
    }
    CHECK(hgic.rxcookie == 0 && cookie_warnings == 0);
    header(buffer, HGIC_HDR_TYPE_FRM2, 23);
    buffer[20] = 0x66;
    buffer[21] = 0xaa;
    put_unaligned_le16(1, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 23, &packet));
    CHECK(!packet.cookie_discontinuity && hgic.rxcookie == 1);
    robusto_halow_spi_reset_rx_cookie();
    put_unaligned_le16(100, buffer + 6);
    buffer[20] = 0;
    CHECK(!robusto_halow_spi_parse(buffer, 23, &packet));
    buffer[20] = 0x66;
    put_unaligned_le16(0, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, 23, &packet));
    CHECK(!packet.cookie_discontinuity && hgic.rxcookie == 0);

    header(buffer, HGIC_HDR_TYPE_CMD2, CMD_HDR_LEN + 3);
    put_unaligned_le16(HGIC_CMD_GET_MODE, buffer + 8);
    memcpy(buffer + CMD_HDR_LEN, "ap", 3);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 3, &packet));
    CHECK(packet.command && packet.ap_mode);
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 2, &packet));
    put_unaligned_le16(CMD_HDR_LEN + 4, buffer + 4);
    memcpy(buffer + CMD_HDR_LEN, "sta", 4);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 4, &packet));
    CHECK(!packet.ap_mode);
    memcpy(buffer + CMD_HDR_LEN, "bad", 4);
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 4, &packet));

    /* Reported command-145 header: two declared payload bytes, padded read. */
    const uint8_t mode_reply[] = {
        0x1a, 0x2b, 0x03, 0x01, 0x0e, 0x00, 0x00, 0x00,
        0x91, 0x00, 0x02, 0x00, 'a', 'p', 0xa5, 0xa5
    };
    memcpy(buffer, mode_reply, sizeof(mode_reply));
    CHECK(robusto_halow_spi_parse(buffer, sizeof(mode_reply), &packet));
    CHECK(packet.command_id == HGIC_CMD_GET_MODE && packet.command_status == 2);
    CHECK(packet.ap_mode);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 2, &packet));
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 1, &packet));
    put_unaligned_le16(CMD_HDR_LEN + 1, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, sizeof(mode_reply), &packet));
    put_unaligned_le16(CMD_HDR_LEN, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, sizeof(mode_reply), &packet));
    put_unaligned_le16(CMD_HDR_LEN + 3, buffer + 4);
    memcpy(buffer + CMD_HDR_LEN, "apx", 3);
    CHECK(!robusto_halow_spi_parse(buffer, sizeof(mode_reply), &packet));
    memcpy(buffer + CMD_HDR_LEN, "sta", 3);
    CHECK(robusto_halow_spi_parse(buffer, sizeof(mode_reply), &packet));
    CHECK(!packet.ap_mode);

    const uint16_t settings[] = {
        HGIC_CMD_SET_SSID, HGIC_CMD_SET_WPA_PSK, HGIC_CMD_SET_KEY_MGMT,
        HGIC_CMD_SET_BSS_BW, HGIC_CMD_SET_CHAN_LIST, HGIC_CMD_RADIO_ONOFF,
        HGIC_CMD_SET_FRG_THRESHOLD, HGIC_CMD_SET_ATCMD,
        HGIC_CMD_GET_SSID, HGIC_CMD_GET_KEY_MGMT,
        HGIC_CMD_GET_BSS_BW, HGIC_CMD_GET_CHAN_LIST
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        header(buffer, HGIC_HDR_TYPE_CMD2, CMD_HDR_LEN + 2);
        put_unaligned_le16(settings[i], buffer + 8);
        memcpy(buffer + CMD_HDR_LEN, "ab", 2);
        buffer[CMD_HDR_LEN + 2] = 0xa5;
        CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 4, &packet));
        CHECK(packet.command_id == settings[i] && packet.command_length == 2);
        CHECK(packet.command_payload == buffer + CMD_HDR_LEN);
        CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 1, &packet));
        put_unaligned_le16(0xffff, buffer + 10);
        CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 2, &packet));
        CHECK(packet.command_status == -1);
    }

    header(buffer, HGIC_HDR_TYPE_CMD, CMD_HDR_LEN);
    buffer[8] = HGIC_CMD_GET_CONN_STATE;
    put_unaligned_le16(9, buffer + 10);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(packet.command && packet.command_id == HGIC_CMD_GET_CONN_STATE);
    CHECK(hgic.status.conn_state == 1);
    put_unaligned_le16(8, buffer + 10);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(hgic.status.conn_state == 0);
    put_unaligned_le16(0xffff, buffer + 10);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(packet.command_status == -1);
    header(buffer, HGIC_HDR_TYPE_CMD2, CMD_HDR_LEN);
    put_unaligned_le16(HGIC_CMD_GET_FW_INFO, buffer + 8);
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    put_unaligned_le16(CMD_HDR_LEN + sizeof(hgic.fwinfo), buffer + 4);
    put_unaligned_le32(0x01060405, buffer + CMD_HDR_LEN);
    buffer[CMD_HDR_LEN + 12] = 0xa5; /* fwinfo.mac[0] */
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + sizeof(hgic.fwinfo), &packet));
    CHECK(hgic.fwinfo.version == 0x01060405);
    CHECK(hgic.addr[0] == 0xa5);
    header(buffer, HGIC_HDR_TYPE_CMD, CMD_HDR_LEN);
    buffer[8] = HGIC_CMD_GET_WPA_PSK;
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));

    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN);
    buffer[8] = HGIC_EVENT_CONECT_START;
    memset(buffer + CMD_HDR_LEN, 0xa5, 4);
    CHECK(robusto_halow_spi_parse(buffer, 16, &packet));
    CHECK(packet.event && packet.event_id == HGIC_EVENT_CONECT_START && !packet.unsupported);
    CHECK(packet.event_length == 0 && packet.frame == NULL && packet.length == 0);
    const uint8_t details[] = {0, 0xa5, 0xff};
    for (unsigned int type = 0; type < 2; ++type) {
        for (unsigned int index = 0; index < sizeof(details); ++index) {
            header(buffer, type == 0 ? HGIC_HDR_TYPE_EVENT : HGIC_HDR_TYPE_EVENT2,
                   CMD_HDR_LEN + 1);
            put_unaligned_le16(HGIC_EVENT_CONNECT_FAIL, buffer + 8);
            buffer[CMD_HDR_LEN] = details[index];
            memset(buffer + CMD_HDR_LEN + 1, 0x5a, 3);
            hgic.status.conn_state = 1;
            CHECK(robusto_halow_spi_parse(buffer, 16, &packet));
            CHECK(packet.event && packet.event_id == HGIC_EVENT_CONNECT_FAIL);
            CHECK(!packet.unsupported && packet.event_length == 1);
            CHECK(packet.event_payload == buffer + CMD_HDR_LEN);
            CHECK(packet.event_payload[0] == details[index]);
            CHECK(packet.frame == NULL && packet.length == 0);
            CHECK(hgic.status.conn_state == 1);
            CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 1, &packet));
            CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
            put_unaligned_le16(CMD_HDR_LEN, buffer + 4);
            CHECK(!robusto_halow_spi_parse(buffer, 16, &packet));
            CHECK(!packet.unsupported);
        }
    }
    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN + 3);
    buffer[8] = HGIC_EVENT_CONNECT_FAIL;
    CHECK(robusto_halow_spi_parse(buffer, 16, &packet));
    CHECK(packet.event_length == 3);

    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN + 5);
    buffer[8] = HGIC_EVENT_CONECTED;
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 5, &packet));
    put_unaligned_le16(CMD_HDR_LEN + 6, buffer + 4);
    buffer[CMD_HDR_LEN] = 0xab;
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 6, &packet));
    CHECK(hgic.status.conn_state == 1 && hgic.bssid[0] == 0xab);
    buffer[8] = HGIC_EVENT_DISCONECTED;
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 6, &packet));
    CHECK(hgic.status.conn_state == 0 && hgic.bssid[0] == 0);
    buffer[8] = HGIC_EVENT_SIGNAL;
    buffer[CMD_HDR_LEN] = 0xd8;
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 6, &packet));
    CHECK(hgic.status.rssi == -40);
    put_unaligned_le16(CMD_HDR_LEN + 1, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 1, &packet));
    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN);
    buffer[8] = HGIC_EVENT_FWDBG_INFO;
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
    CHECK(!packet.unsupported && packet.event && packet.event_id == HGIC_EVENT_FWDBG_INFO);
    buffer[8] = HGIC_EVENT_REQUEST_PARAM;
    CHECK(robusto_halow_spi_parse(buffer, 16, &packet));
    CHECK(!packet.unsupported && packet.event_id == HGIC_EVENT_REQUEST_PARAM);
    const size_t minimum_event_lengths[32] = {
        [HGIC_EVENT_TX_BITRATE] = 4, [HGIC_EVENT_CONECTED] = 6,
        [HGIC_EVENT_SIGNAL] = 2, [HGIC_EVENT_CONNECT_FAIL] = 1,
        [HGIC_EVENT_EXCEPTION_INFO] = 4
    };
    for (unsigned int type = 0; type < 2; ++type) {
        for (unsigned int id = 1; id <= 31; ++id) {
            size_t minimum = minimum_event_lengths[id];
            CHECK(robusto_halow_spi_event_name(id) != NULL);
            header(buffer, type == 0 ? HGIC_HDR_TYPE_EVENT : HGIC_HDR_TYPE_EVENT2,
                   CMD_HDR_LEN + minimum);
            put_unaligned_le16(id, buffer + 8);
            memset(buffer + CMD_HDR_LEN + minimum, 0xa5, 3);
            hgic.status.conn_state = 1;
            CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + minimum + 3, &packet));
            CHECK(packet.event && packet.event_id == id && !packet.unsupported);
            CHECK(packet.event_payload == buffer + CMD_HDR_LEN && packet.event_length == minimum);
            CHECK(packet.frame == NULL && packet.length == 0);
            if (id != HGIC_EVENT_CONECTED && id != HGIC_EVENT_DISCONECTED)
                CHECK(hgic.status.conn_state == 1);
            if (minimum > 0) {
                put_unaligned_le16(CMD_HDR_LEN + minimum - 1, buffer + 4);
                CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + minimum + 3, &packet));
                CHECK(!packet.unsupported);
            }
        }
        const uint16_t unknown[] = {0, 32, 255, 256};
        for (unsigned int i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
            header(buffer, type == 0 ? HGIC_HDR_TYPE_EVENT : HGIC_HDR_TYPE_EVENT2, CMD_HDR_LEN);
            put_unaligned_le16(unknown[i], buffer + 8);
            CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN, &packet));
            CHECK(packet.event && packet.unsupported);
        }
    }
    CHECK(strcmp(robusto_halow_spi_event_name(2), "CH_SWICH") == 0);
    CHECK(strcmp(robusto_halow_spi_event_name(30), "ACS_DONE") == 0);
    CHECK(strcmp(robusto_halow_spi_event_name(31), "UNDOCUMENTED_31") == 0);
    CHECK(robusto_halow_spi_event_name(32) == NULL);
    const size_t exception_lengths[] = {4, 4, 12, 12, 4, 16, 16, 8, 4};
    for (unsigned int code = 1; code <= 8; ++code) {
        size_t minimum = exception_lengths[code];
        for (size_t length = 0; length <= minimum; ++length) {
            header(buffer, HGIC_HDR_TYPE_EVENT2, CMD_HDR_LEN + length);
            put_unaligned_le16(HGIC_EVENT_EXCEPTION_INFO, buffer + 8);
            put_unaligned_le32(code, buffer + CMD_HDR_LEN);
            CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + minimum, &packet) ==
                  (length == minimum));
        }
    }
    for (size_t length = 0; length <= sizeof(hgic.paired_stas) + 6; ++length) {
        header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN + length);
        buffer[8] = HGIC_EVENT_PAIR_DONE;
        memset(buffer + CMD_HDR_LEN, 0xab, length);
        bool valid = length <= sizeof(hgic.paired_stas) && length % 6 == 0;
        CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + length, &packet) == valid);
        if (valid) {
            CHECK(hgic.paired_stas_cnt == length / 6);
            if (length > 0) CHECK(hgic.paired_stas[length - 1] == 0xab);
            if (length < sizeof(hgic.paired_stas)) CHECK(hgic.paired_stas[length] == 0);
        }
    }
    header(buffer, HGIC_HDR_TYPE_EVENT, CMD_HDR_LEN + 4);
    buffer[8] = HGIC_EVENT_TX_BITRATE;
    put_unaligned_le32(123456, buffer + CMD_HDR_LEN);
    CHECK(robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 4, &packet));
    CHECK(packet.event && packet.event_id == HGIC_EVENT_TX_BITRATE);
    CHECK(hgic.status.tx_bitrate == 123456);
    put_unaligned_le16(CMD_HDR_LEN + 3, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 3, &packet));
    CHECK(!packet.unsupported);
    buffer[0] = 0;
    CHECK(!robusto_halow_spi_parse(buffer, CMD_HDR_LEN + 3, &packet));
    CHECK(!packet.unsupported && !packet.event);
    uint8_t query[18], response[18], status, value;
    robusto_halow_spi_register_query(query, 0x110);
    CHECK(query[0] == 0xff && query[1] == 0x74 && query[2] == 0);
    CHECK(query[3] == 2 && query[4] == 0x20 && query[5] == 0);
    CHECK((query[6] & 1) == 1);
    for (unsigned int i = 7; i < sizeof(query); ++i) {
        CHECK(query[i] == 0xff);
    }
    memset(response, 0xff, sizeof(response));
    CHECK(!robusto_halow_spi_register_response(response, &status, &value));
    response[8] = 0;
    response[9] = 64;
    CHECK(robusto_halow_spi_register_response(response, &status, &value));
    CHECK(status == 0 && value == 64);
    response[8] = 0x08;
    CHECK(robusto_halow_spi_register_response(response, &status, &value));
    CHECK(status == 0x08);
    memset(response, 0xff, sizeof(response));
    response[17] = 0; /* Missing the second R5 byte. */
    CHECK(!robusto_halow_spi_register_response(response, &status, &value));
    response[7] = 0x80;
    CHECK(!robusto_halow_spi_register_response(response, &status, &value));
    header(buffer, HGIC_HDR_TYPE_SOFTFC, sizeof(struct hgic_hdr));
    put_unaligned_le16(7, buffer + 6);
    CHECK(robusto_halow_spi_parse(buffer, sizeof(struct hgic_hdr), &packet));
    CHECK(packet.flow_control && packet.tx_window == 7);
    put_unaligned_le16(9, buffer + 4);
    CHECK(!robusto_halow_spi_parse(buffer, 9, &packet));
    memset(hgic.addr, 0xa5, 6);
    unsigned char destination[6] = {1, 2, 3, 4, 5, 6};
    CHECK(hgic_raw_send(destination, buffer, ROBUSTO_HALOW_SPI_FRAME_MAX) == 1664);
    CHECK(submitted_length == 1664);
    CHECK(get_unaligned_le16(hgic.tx_buf) == HGIC_HDR_TX_MAGIC);
    CHECK(get_unaligned_le16(hgic.tx_buf + 4) == 1664);
    CHECK(hgic.tx_buf[8] == 1 && hgic.tx_buf[13] == 6);
    CHECK(hgic.tx_buf[14] == 0xa5);
    CHECK(hgic.tx_buf[20] == 0x66 && hgic.tx_buf[21] == 0xaa);
    memset(hgic.cmd_buf, 0, sizeof(hgic.cmd_buf));
    test_sdspi = true;
    CHECK(hgic_raw_get_fwinfo() == CMD_HDR_LEN);
    CHECK(!unexpected_transfer);
    CHECK(cmd53_count == 1 && block_count == 1 && stop_count == 1);
    CHECK(cs_assert_count == 1 && cs_release_count == 1 && !cs_active);
    const char expected_sequence[] = "+CBRRSR-";
    CHECK(busy_read_count == 3);
    CHECK(transfer_sequence_length == sizeof(expected_sequence) - 1);
    for (unsigned int i = 0; i < sizeof(expected_sequence) - 1; ++i) {
        CHECK(transfer_sequence[i] == expected_sequence[i]);
    }
    CHECK(submitted_length == CMD_HDR_LEN);
    const unsigned char expected_header[] = {
        0x2b, 0x1a, 0x03, 0x01, 0x0c, 0, 0, 0, 0x2b, 0, 0, 0
    };
    CHECK(CMD_HDR_LEN == sizeof(expected_header));
    for (unsigned int i = 0; i < sizeof(submitted_block); ++i) {
        CHECK(submitted_block[i] ==
              (i < sizeof(expected_header) ? expected_header[i] : 0));
    }
    const unsigned char control_headers[3][15] = {
        {0x2b, 0x1a, 0x03, 0x01, 0x0f, 0, 0, 0, 0x29, 0, 0, 0, 's', 't', 'a'},
        /* The header-only query retains STA bytes beyond its declared length. */
        {0x2b, 0x1a, 0x03, 0x01, 0x0c, 0, 0, 0, 0x28, 0, 0, 0, 's', 't', 'a'},
        {0x2b, 0x1a, 0x03, 0x01, 0x0c, 0, 0, 0, 0x84, 0, 0, 0, 's', 't', 'a'}
    };
    for (unsigned int command_index = 0; command_index < 3; ++command_index) {
        cmd53_count = block_count = stop_count = 0;
        cs_assert_count = cs_release_count = 0;
        transfer_sequence_length = busy_read_count = 0;
        unexpected_transfer = false;
        const unsigned int command_length = command_index == 0 ? CMD_HDR_LEN + 3 : CMD_HDR_LEN;
        int sent = command_index == 0 ? hgic_raw_set_mode("sta")
                   : command_index == 1 ? hgic_raw_get_connect_state()
                                        : hgic_raw_mcu_reset();
        CHECK(sent == (int)command_length);
        CHECK(submitted_length == command_length);
        CHECK(!unexpected_transfer);
        CHECK(cmd53_count == 1 && block_count == 1 && stop_count == 1);
        CHECK(cs_assert_count == 1 && cs_release_count == 1 && !cs_active);
        CHECK(busy_read_count == 3);
        CHECK(transfer_sequence_length == sizeof(expected_sequence) - 1);
        for (unsigned int i = 0; i < sizeof(expected_sequence) - 1; ++i) {
            CHECK(transfer_sequence[i] == expected_sequence[i]);
        }
        for (unsigned int i = 0; i < sizeof(submitted_block); ++i) {
            CHECK(submitted_block[i] ==
                  (i < sizeof(control_headers[command_index]) ? control_headers[command_index][i] : 0));
        }
    }
    cmd53_count = block_count = stop_count = 0;
    cs_assert_count = cs_release_count = 0;
    transfer_sequence_length = busy_read_count = 0;
    unexpected_transfer = false;
    CHECK(hgic_raw_set_radio_onoff(1) == CMD_HDR_LEN + 1);
    const unsigned char radio_command[] = {
        0x2b, 0x1a, 0x03, 0x01, 0x0d, 0, 0, 0, 0x41, 0, 0, 0, 1
    };
    CHECK(submitted_length == sizeof(radio_command) && !unexpected_transfer);
    CHECK(cmd53_count == 1 && block_count == 1 && stop_count == 1);
    CHECK(cs_assert_count == 1 && cs_release_count == 1 && !cs_active);
    for (size_t i = 0; i < sizeof(submitted_block); ++i) {
        CHECK(submitted_block[i] == (i < sizeof(radio_command) ? radio_command[i] : 0));
    }
    return 0;
}

#ifdef ROBUSTO_HALOW_SPI_HOST_TEST
int main(void)
{
    return test_main();
}
#endif
