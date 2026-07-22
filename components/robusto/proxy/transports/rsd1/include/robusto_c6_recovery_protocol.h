#pragma once

#include <stdint.h>

#define ROBUSTO_C6_RECOVERY_IDENTITY_MAGIC 0x42304944U
#define ROBUSTO_C6_RECOVERY_IDENTITY_VERSION 1U
#define ROBUSTO_C6_RECOVERY_IDENTITY_REQUEST_MSG_ID 0x42300001U
#define ROBUSTO_C6_RECOVERY_IDENTITY_RECORD_MSG_ID 0x42300002U
#define ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_READ 1U
#define ROBUSTO_C6_RECOVERY_IDENTITY_COMMAND_CONFIRM 2U
#define ROBUSTO_C6_RECOVERY_PROTOCOL_REVISION 1U
#define ROBUSTO_C6_RECOVERY_FRONTEND_ESP_HOSTED 1U
#define ROBUSTO_C6_RECOVERY_FRONTEND_RAW_SDIO 2U
#define ROBUSTO_C6_RECOVERY_TARGET_ESP32_C6 1U
#define ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE 32U
#define ROBUSTO_C6_RECOVERY_BOOT_PENDING_VERIFY 1U
#define ROBUSTO_C6_RECOVERY_BOOT_CONFIRMED 2U

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define ROBUSTO_C6_RECOVERY_PACKED
#else
#define ROBUSTO_C6_RECOVERY_PACKED __attribute__((packed))
#endif

typedef struct ROBUSTO_C6_RECOVERY_PACKED {
    uint32_t magic;
    uint16_t version;
    uint16_t command;
    uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE];
} robusto_c6_recovery_request_t;

typedef struct ROBUSTO_C6_RECOVERY_PACKED {
    uint32_t magic;
    uint16_t version;
    uint8_t generation;
    uint8_t protocol_revision;
    uint8_t updater_revision;
    uint8_t transport_frontend;
    uint8_t target_chip;
    uint8_t partition_subtype;
    uint8_t boot_state;
    uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE];
} robusto_c6_recovery_record_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif
#undef ROBUSTO_C6_RECOVERY_PACKED

typedef char robusto_c6_recovery_request_layout_must_be_40_bytes[
    sizeof(robusto_c6_recovery_request_t) == 40 ? 1 : -1];
typedef char robusto_c6_recovery_record_layout_must_be_45_bytes[
    sizeof(robusto_c6_recovery_record_t) == 45 ? 1 : -1];