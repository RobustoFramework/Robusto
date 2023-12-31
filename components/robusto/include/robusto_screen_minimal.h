#include "robconfig.h"
#include "inttypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

void robusto_screen_minimal_write(char * txt, uint8_t col, uint8_t row);
void robusto_screen_minimal_clear();
void robusto_screen_minimal_init(char * _log_prefix);


#ifdef __cplusplus
} /* extern "C" */
#endif
