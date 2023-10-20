#pragma once
#include <robconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern TaskHandle_t umts_modem_setup_task;

extern char *operator_name;


int get_sync_attempts();
void umts_start(char * _log_prefix);
void handle_umts_states(int state);
void umts_abort_if_shutting_down();
void umts_cleanup();
