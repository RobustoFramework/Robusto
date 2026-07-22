#ifndef ROBUSTO_C6_UPDATE_PROTOCOL_H
#define ROBUSTO_C6_UPDATE_PROTOCOL_H

#include <stdint.h>

#define ROBUSTO_C6_UPDATE_MAGIC 0x41355550U
#define ROBUSTO_C6_UPDATE_VERSION 1U
#define ROBUSTO_C6_UPDATE_REQUEST_MSG_ID 0x41350001U
#define ROBUSTO_C6_UPDATE_RESPONSE_MSG_ID 0x41350002U
#define ROBUSTO_C6_UPDATE_QUEUE_CAPACITY 1U
#define ROBUSTO_C6_UPDATE_MAX_CHUNK_SIZE 1500U
#define ROBUSTO_C6_UPDATE_GENERATION_CURRENT 6U
#define ROBUSTO_C6_UPDATER_REVISION_LEGACY 1U
#define ROBUSTO_C6_UPDATER_REVISION_CURRENT 2U

#define ROBUSTO_C6_UPDATE_COMMAND_STATUS 0U
#define ROBUSTO_C6_UPDATE_COMMAND_BEGIN 1U
#define ROBUSTO_C6_UPDATE_COMMAND_WRITE 2U
#define ROBUSTO_C6_UPDATE_COMMAND_END 3U
#define ROBUSTO_C6_UPDATE_COMMAND_ACTIVATE 4U
#define ROBUSTO_C6_UPDATE_COMMAND_ABORT 5U

#define ROBUSTO_C6_UPDATE_STATE_IDLE 0U
#define ROBUSTO_C6_UPDATE_STATE_RECEIVING 1U
#define ROBUSTO_C6_UPDATE_STATE_FINALIZED 2U
#define ROBUSTO_C6_UPDATE_STATE_ACTIVATED 3U
#define ROBUSTO_C6_UPDATE_STATE_FAILED 4U

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define ROBUSTO_C6_PACKED
#else
#define ROBUSTO_C6_PACKED __attribute__((packed))
#endif

typedef struct ROBUSTO_C6_PACKED {
    uint32_t magic;
    uint16_t version;
    uint8_t command;
    uint8_t reserved;
    uint32_t transaction_id;
    uint32_t offset;
    uint32_t total_size;
    uint16_t data_size;
    uint16_t reserved2;
    uint8_t sha256[32];
} robusto_c6_update_request_t;

typedef struct ROBUSTO_C6_PACKED {
    uint32_t magic;
    uint16_t version;
    uint8_t command;
    uint8_t state;
    uint32_t transaction_id;
    int32_t status;
    uint32_t next_offset;
    uint8_t partition_subtype;
    uint8_t generation;
    uint8_t updater_revision;
    uint8_t reserved;
    uint32_t total_size;
} robusto_c6_update_response_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif
#undef ROBUSTO_C6_PACKED

typedef char robusto_c6_update_request_layout_must_be_56_bytes[
    sizeof(robusto_c6_update_request_t) == 56 ? 1 : -1];
typedef char robusto_c6_update_response_layout_must_be_28_bytes[
    sizeof(robusto_c6_update_response_t) == 28 ? 1 : -1];

#endif