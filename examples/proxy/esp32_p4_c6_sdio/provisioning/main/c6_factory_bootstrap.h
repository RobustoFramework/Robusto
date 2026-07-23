#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct c6_factory_bootstrap_config {
    const char *partition_label;
    size_t image_offset;
    size_t image_size;
    const uint8_t *image_sha256;
} c6_factory_bootstrap_config_t;

esp_err_t c6_factory_bootstrap_install(
    const c6_factory_bootstrap_config_t *config);
