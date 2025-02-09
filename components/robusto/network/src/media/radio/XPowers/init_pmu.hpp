#pragma once
#include <robconfig.h>
// make sure this is only compiled when needed
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#ifdef __cplusplus
extern "C"
{
#endif

void init_pmu(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif