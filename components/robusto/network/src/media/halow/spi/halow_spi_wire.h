#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROBUSTO_HALOW_SPI_FRAME_MAX 1642U
#define ROBUSTO_HALOW_SPI_RX_CAPACITY 2048U
#define ROBUSTO_HALOW_SPI_AP_PROBE "RHAP"
#define ROBUSTO_HALOW_SPI_EVENT_UNDOCUMENTED_31 31U

typedef struct {
    const uint8_t *frame;
    size_t length;
    uint8_t source[6];
    bool command;
    uint16_t command_id;
    int16_t command_status;
    const uint8_t *command_payload;
    size_t command_length;
    bool flow_control;
    uint16_t tx_window;
    bool unsupported;
    bool event;
    uint16_t event_id;
    const uint8_t *event_payload;
    size_t event_length;
    bool ap_mode;
    bool loopback;
    bool loopback_valid;
} robusto_halow_spi_packet_t;

/* Caller serializes this and all hgic accesses; buffer remains owned by caller. */
bool robusto_halow_spi_parse(uint8_t *buffer, size_t length,
                           robusto_halow_spi_packet_t *packet);

const char *robusto_halow_spi_event_name(uint16_t id);

/* Read-only function-0 CMD52 diagnostics, using the vendor's framing and CRC. */
void robusto_halow_spi_register_query(uint8_t request[18], uint32_t address);
bool robusto_halow_spi_register_response(const uint8_t response[18],
                                      uint8_t *status, uint8_t *value);
