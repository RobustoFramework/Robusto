#include "halow_spi_wire.h"
#include "hgic_raw.h"
#include <string.h>

extern struct hgic_raw hgic;
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
        unsigned char *data = buffer;
        unsigned int data_length = wire_length;
        if (hgic_raw_rx(&data, &data_length) != HGIC_RAW_RX_TYPE_DATA ||
            data_length != wire_length - sizeof(struct hgic_frm_hdr2)) {
            return false;
        }
        memcpy(packet->source, data + 6, 6);
        packet->frame = data + sizeof(struct eth_hdr);
        packet->length = data_length - sizeof(struct eth_hdr);
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
        switch (id) {
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
        default:
            packet->unsupported = true;
            return false;
        }
        return true;
    }
    packet->unsupported = true;
    return false;
}
