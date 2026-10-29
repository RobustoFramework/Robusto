#include "halow_spi_wire.h"
#include "hgic_raw.h"
#include <string.h>

extern struct hgic_raw hgic;
static bool rx_cookie_valid;
extern unsigned char crc7_be(unsigned char crc, const unsigned char *buffer,
                            unsigned int length);

void robusto_halow_spi_register_query(uint8_t request[18], uint32_t address)
{
    memset(request, 0xff, 18);
    request[1] = 0x74; /* CMD52, function 0, read, no read-after-write. */
    request[2] = (uint8_t)(address >> 15);
    request[3] = (uint8_t)(address >> 7);
    request[4] = (uint8_t)(address << 1);
    request[5] = 0;
    request[6] = crc7_be(0, request + 1, 5) | 1;
}

bool robusto_halow_spi_register_response(const uint8_t response[18],
                                      uint8_t *status, uint8_t *value)
{
    for (size_t i = 7; i + 1 < 18; ++i) {
        if (response[i] != 0xff) {
            *status = response[i];
            *value = response[i + 1];
            return (response[i] & 0x80) == 0;
        }
    }
    return false;
}

_Static_assert(sizeof(struct hgic_frm_hdr2) + sizeof(struct eth_hdr) == 22,
               "Vendor raw frame overhead changed");

void robusto_halow_spi_reset_rx_cookie(void)
{
    rx_cookie_valid = false;
    hgic.rxcookie = 0;
}

const char *robusto_halow_spi_event_name(uint16_t id)
{
#define EVENT_NAME(name) case HGIC_EVENT_##name: return #name
    switch (id) {
    EVENT_NAME(STATE_CHG);
    EVENT_NAME(CH_SWICH);
    EVENT_NAME(DISCONNECT_REASON);
    EVENT_NAME(ASSOC_STATUS);
    EVENT_NAME(SCANNING);
    EVENT_NAME(SCAN_DONE);
    EVENT_NAME(TX_BITRATE);
    EVENT_NAME(PAIR_START);
    EVENT_NAME(PAIR_SUCCESS);
    EVENT_NAME(PAIR_DONE);
    EVENT_NAME(CONECT_START);
    EVENT_NAME(CONECTED);
    EVENT_NAME(DISCONECTED);
    EVENT_NAME(SIGNAL);
    EVENT_NAME(DISCONNET_LOG);
    EVENT_NAME(REQUEST_PARAM);
    EVENT_NAME(TESTMODE_STATE);
    EVENT_NAME(FWDBG_INFO);
    EVENT_NAME(CUSTOMER_MGMT);
    EVENT_NAME(SLEEP_FAIL);
    EVENT_NAME(DHCPC_DONE);
    EVENT_NAME(CONNECT_FAIL);
    EVENT_NAME(CUST_DRIVER_DATA);
    EVENT_NAME(UNPAIR_STA);
    EVENT_NAME(BLENC_DATA);
    EVENT_NAME(HWSCAN_RESULT);
    EVENT_NAME(EXCEPTION_INFO);
    EVENT_NAME(DSLEEP_WAKEUP);
    EVENT_NAME(STA_MIC_ERROR);
    EVENT_NAME(ACS_DONE);
    case ROBUSTO_HALOW_SPI_EVENT_UNDOCUMENTED_31:
        return "UNDOCUMENTED_31";
    default:
        return NULL;
    }
#undef EVENT_NAME
}

static bool valid_exception(const uint8_t *payload, size_t length)
{
    if (length < 4) return false;
    switch (get_unaligned_le32(payload)) {
    case HGIC_EXCEPTION_TXDELAY_TOOLONG:
    case HGIC_EXCEPTION_STRONG_BGRSSI:
        return length >= 16;
    case HGIC_EXCEPTION_HEAP_USED_OVERTOP:
    case HGIC_EXCEPTION_WIFI_BUFFER_USED_OVERTOP:
        return length >= 12;
    case HGIC_EXCEPTION_TEMPERATURE_OVERTOP:
        return length >= 8;
    default:
        return true;
    }
}

bool robusto_halow_spi_parse(uint8_t *buffer, size_t length,
                           robusto_halow_spi_packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    if (length < sizeof(struct hgic_hdr) || length > ROBUSTO_HALOW_SPI_RX_CAPACITY ||
        get_unaligned_le16(buffer) != HGIC_HDR_RX_MAGIC) {
        return false;
    }
    const size_t wire_length = get_unaligned_le16(buffer + 4);
    if (wire_length < sizeof(struct hgic_hdr) || wire_length > length) {
        return false;
    }
    uint8_t type = buffer[2];
    if (type == HGIC_HDR_TYPE_TEST2) {
        packet->loopback = true;
        packet->loopback_payload = buffer + sizeof(struct hgic_hdr);
        packet->loopback_length = wire_length - sizeof(struct hgic_hdr);
        packet->loopback_valid =
            wire_length == sizeof(struct hgic_hdr) + sizeof(ROBUSTO_HALOW_SPI_AP_PROBE) - 1 &&
            memcmp(buffer + sizeof(struct hgic_hdr), ROBUSTO_HALOW_SPI_AP_PROBE,
                   sizeof(ROBUSTO_HALOW_SPI_AP_PROBE) - 1) == 0;
        return true;
    }
    if (type == HGIC_HDR_TYPE_SOFTFC) {
        if (wire_length != sizeof(struct hgic_hdr)) {
            return false;
        }
        packet->flow_control = true;
        packet->tx_window = get_unaligned_le16(buffer + 6);
        return true;
    }
    if (type == HGIC_HDR_TYPE_FRM2) {
        const size_t overhead = sizeof(struct hgic_frm_hdr2) + sizeof(struct eth_hdr);
        if (wire_length <= overhead || wire_length - overhead > ROBUSTO_HALOW_SPI_FRAME_MAX ||
            buffer[20] != (HGIC_RAW_ETHTYPE >> 8) ||
            buffer[21] != (HGIC_RAW_ETHTYPE & 0xff)) {
            return false;
        }
        packet->cookie_previous = hgic.rxcookie;
        packet->cookie_expected = (uint16_t)(hgic.rxcookie + 1U);
        packet->cookie_received = get_unaligned_le16(buffer + 6);
        packet->cookie_discontinuity = rx_cookie_valid &&
            packet->cookie_received != packet->cookie_expected;
        hgic.rxcookie = packet->cookie_received;
        rx_cookie_valid = true;
        const uint8_t *data = buffer + sizeof(struct hgic_frm_hdr2);
        memcpy(packet->source, data + 6, 6);
        packet->frame = data + sizeof(struct eth_hdr);
        packet->length = wire_length - overhead;
        return true;
    }
    if (wire_length < CMD_HDR_LEN) {
        return false;
    }
    const uint8_t *payload = buffer + CMD_HDR_LEN;
    const size_t payload_length = wire_length - CMD_HDR_LEN;
    const uint16_t id = (type == HGIC_HDR_TYPE_CMD2 || type == HGIC_HDR_TYPE_EVENT2)
                            ? get_unaligned_le16(buffer + 8) : buffer[8];
    if (type == HGIC_HDR_TYPE_CMD || type == HGIC_HDR_TYPE_CMD2) {
        const int16_t status = (int16_t)get_unaligned_le16(buffer + 10);
        packet->command = true;
        packet->command_id = id;
        packet->command_status = status;
        packet->command_payload = payload;
        packet->command_length = payload_length;
        if (status < 0) {
            return true;
        }
        switch (id) {
        case HGIC_CMD_GET_FW_INFO:
            if (payload_length < sizeof(hgic.fwinfo)) {
                return false;
            }
            memcpy(&hgic.fwinfo, payload, sizeof(hgic.fwinfo));
            memcpy(hgic.addr, hgic.fwinfo.mac, 6);
            break;
        case HGIC_CMD_GET_CONN_STATE:
            /* Vendor hgic_raw_rx_cmd_resp defines associated as status == 9. */
            hgic.status.conn_state = status == 9;
            break;
        case HGIC_CMD_SET_WORK_MODE:
        case HGIC_CMD_SET_FRG_THRESHOLD:
        case HGIC_CMD_RADIO_ONOFF:
        case HGIC_CMD_SET_SSID:
        case HGIC_CMD_SET_KEY_MGMT:
        case HGIC_CMD_SET_WPA_PSK:
        case HGIC_CMD_SET_BSS_BW:
        case HGIC_CMD_SET_CHAN_LIST:
        case HGIC_CMD_SET_ATCMD:
        case HGIC_CMD_GET_SSID:
        case HGIC_CMD_GET_KEY_MGMT:
        case HGIC_CMD_GET_BSS_BW:
        case HGIC_CMD_GET_CHAN_LIST:
            break;
        case HGIC_CMD_GET_MODE:
            if (payload_length >= 2 && memcmp(payload, "ap", 2) == 0 &&
                (payload_length == 2 || payload[2] == '\0')) {
                packet->ap_mode = true;
            } else if (payload_length < 3 || memcmp(payload, "sta", 3) != 0 ||
                       (payload_length > 3 && payload[3] != '\0')) {
                return false;
            }
            break;
        default:
            packet->unsupported = true;
            return false;
        }
        return true;
    }
    if (type == HGIC_HDR_TYPE_EVENT || type == HGIC_HDR_TYPE_EVENT2) {
        packet->event = true;
        packet->event_id = id;
        packet->event_payload = payload;
        packet->event_length = payload_length;
        if (robusto_halow_spi_event_name(id) == NULL) {
            packet->unsupported = true;
            return false;
        }
        switch (id) {
        case HGIC_EVENT_CONECT_START:
            break;
        case HGIC_EVENT_CONNECT_FAIL:
            if (payload_length < 1) {
                return false;
            }
            break;
        case HGIC_EVENT_TX_BITRATE:
            if (payload_length < 4) {
                return false;
            }
            hgic.status.tx_bitrate = get_unaligned_le32(payload);
            break;
        case HGIC_EVENT_CONECTED:
            if (payload_length < 6) {
                return false;
            }
            memcpy(hgic.bssid, payload, 6);
            hgic.status.conn_state = 1;
            break;
        case HGIC_EVENT_DISCONECTED:
            hgic.status.conn_state = 0;
            memset(hgic.bssid, 0, 6);
            break;
        case HGIC_EVENT_SIGNAL:
            if (payload_length < 2) {
                return false;
            }
            hgic.status.rssi = (int8_t)payload[0];
            hgic.status.evm = (int8_t)payload[1];
            break;
        case HGIC_EVENT_PAIR_DONE:
            if (payload_length > sizeof(hgic.paired_stas) || payload_length % 6 != 0) {
                return false;
            }
            memset(hgic.paired_stas, 0, sizeof(hgic.paired_stas));
            memcpy(hgic.paired_stas, payload, payload_length);
            hgic.paired_stas_cnt = payload_length / 6;
            break;
        case HGIC_EVENT_EXCEPTION_INFO:
            if (!valid_exception(payload, payload_length)) return false;
            break;
        default:
            break;
        }
        return true;
    }
    packet->unsupported = true;
    return false;
}
