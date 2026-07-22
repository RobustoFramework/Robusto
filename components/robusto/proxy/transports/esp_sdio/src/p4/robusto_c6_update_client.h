#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"
#include "robusto_c6_recovery_protocol.h"

esp_err_t robusto_c6_update_get_identity(
    robusto_c6_recovery_record_t *identity);
esp_err_t robusto_c6_update_confirm_identity(
    const uint8_t build_sha256[ROBUSTO_C6_RECOVERY_BUILD_SHA256_SIZE],
    robusto_c6_recovery_record_t *identity);
esp_err_t robusto_c6_update_require_revision_2(void);
esp_err_t robusto_c6_update_install(const esp_partition_t *partition,
                                    size_t partition_offset,
                                    size_t image_size,
                                    const uint8_t sha256[32]);