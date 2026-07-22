#pragma once

#include <stdint.h>

#include <robusto_retval.h>

typedef rob_ret_val_t(init_result_callback_t)(void *context, char *log_prefix);
typedef rob_ret_val_t(start_result_callback_t)(void *context);
typedef rob_ret_val_t(stop_result_callback_t)(void *context);

rob_ret_val_t register_service_checked(init_result_callback_t *init_cb,
                                       start_result_callback_t *start_cb,
                                       stop_result_callback_t *stop_cb,
                                       void *context,
                                       uint8_t runlevel,
                                       char *service_name);