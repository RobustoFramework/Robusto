#include "robusto_rsd1_protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int assertions;
static int failures;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(int condition, const char *expression, int line)
{
    ++assertions;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

int main(void)
{
    static uint8_t payload[ROBUSTO_RSD1_MAX_PAYLOAD_SIZE];
    static uint8_t packet[ROBUSTO_RSD1_MAX_PACKET_SIZE];
    robusto_rsd1_packet_view_t decoded;
    size_t packet_size = 0U;

    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)index;
    }

    CHECK(ROBUSTO_RSD1_MAX_PAYLOAD_SIZE == 4120U);
    CHECK(ROBUSTO_RSD1_MAX_PACKET_SIZE == 4144U);
    CHECK(robusto_rsd1_encode(
              packet, sizeof(packet), 0x41340001U, 7U,
              payload, sizeof(payload), &packet_size) == ROBUSTO_RSD1_OK);
    CHECK(packet_size == sizeof(packet));
    CHECK(packet[0] == 'R' && packet[1] == 'S' &&
          packet[2] == 'D' && packet[3] == '1');
    CHECK(packet[4] == ROBUSTO_RSD1_VERSION);
    CHECK(packet[5] == ROBUSTO_RSD1_HEADER_SIZE);
    CHECK(robusto_rsd1_decode(packet, packet_size, &decoded) ==
          ROBUSTO_RSD1_OK);
      CHECK(decoded.message_id == 0x41340001U);
    CHECK(decoded.sequence == 7U);
    CHECK(decoded.payload_size == sizeof(payload));
    CHECK(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    CHECK(robusto_rsd1_decode(packet, packet_size - 1U, &decoded) ==
          ROBUSTO_RSD1_BAD_LENGTH);
    packet[6] = 1U;
    CHECK(robusto_rsd1_decode(packet, packet_size, &decoded) ==
          ROBUSTO_RSD1_BAD_RESERVED);
    packet[6] = 0U;
    packet[ROBUSTO_RSD1_HEADER_SIZE] ^= 1U;
    CHECK(robusto_rsd1_decode(packet, packet_size, &decoded) ==
          ROBUSTO_RSD1_BAD_CRC);
    packet[ROBUSTO_RSD1_HEADER_SIZE] ^= 1U;
    packet[0] = 'X';
    CHECK(robusto_rsd1_decode(packet, packet_size, &decoded) ==
          ROBUSTO_RSD1_BAD_MAGIC);
    packet[0] = 'R';
    packet[4] = 2U;
    CHECK(robusto_rsd1_decode(packet, packet_size, &decoded) ==
          ROBUSTO_RSD1_BAD_VERSION);
    packet[4] = ROBUSTO_RSD1_VERSION;

    CHECK(robusto_rsd1_encode(
              packet, sizeof(packet), 0U, 1U, NULL, 0U, &packet_size) ==
          ROBUSTO_RSD1_INVALID_ARGUMENT);
    CHECK(robusto_rsd1_encode(
              packet, sizeof(packet), 1U, 0U, NULL, 0U, &packet_size) ==
          ROBUSTO_RSD1_INVALID_ARGUMENT);
    CHECK(robusto_rsd1_encode(
              packet, ROBUSTO_RSD1_HEADER_SIZE, 1U, 1U,
              NULL, 0U, &packet_size) == ROBUSTO_RSD1_BAD_LENGTH);

    if (failures != 0) {
      fprintf(stderr, "RSD1 protocol tests failed: %d/%d\n",
                failures, assertions);
        return 1;
    }

      printf("RSD1 protocol tests passed: %d assertions\n", assertions);
    return 0;
}