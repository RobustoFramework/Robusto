#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UI
#include "driver/i2c.h"

void ssd1306_init(i2c_port_t bus, uint16_t dev_addr);

#endif