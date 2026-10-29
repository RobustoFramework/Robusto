#pragma once
#include <robusto_queue.h>

/* Native bindings replace physical drivers but preserve their queue interfaces. */
#ifdef PRESENTATION_CONTRACT_ALL_MEDIA
queue_context_t *espnow_get_queue_context(void);
queue_context_t *lora_get_queue_context(void);
queue_context_t *i2c_get_queue_context(void);
queue_context_t *canbus_get_queue_context(void);
#endif
