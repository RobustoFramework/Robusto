#pragma once
#include <robconfig.h>
#ifdef CONFIG_ROBUSTO_UI
#include "driver/i2c_master.h"

void ssd1306_init(i2c_port_num_t bus, uint16_t dev_addr);

#endif