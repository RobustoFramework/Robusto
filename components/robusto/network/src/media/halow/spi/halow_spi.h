#pragma once

#include <robusto_retval.h>

/* Selected by Kconfig and registered by robusto_halow_init(). */
rob_ret_val_t robusto_halow_spi_register(void);

/* Outgoing worker only, after an initial fragment pass and outside adapter locks. */
void robusto_halow_spi_dump_tx_trace(void);
