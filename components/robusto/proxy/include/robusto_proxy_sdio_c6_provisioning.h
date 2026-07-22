#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct robusto_proxy_sdio_c6_provisioning_config {
    const char *partition_label;
    size_t image_offset;
    size_t image_size;
    const uint8_t *image_sha256;
    const uint8_t *elf_sha256;
} robusto_proxy_sdio_c6_provisioning_config_t;

esp_err_t robusto_proxy_sdio_c6_provision(
    const robusto_proxy_sdio_c6_provisioning_config_t *config,
    bool *restart_required);
