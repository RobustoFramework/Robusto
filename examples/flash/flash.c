#include "flash.h"
#if defined(CONFIG_ROBUSTO_FLASH_EXAMPLE_SPIFFS)
#include <robusto_flash.h>
#include <robusto_logging.h>
#include <string.h>

#ifndef CONFIG_ROBUSTO_FLASH
#error "For this example to work, the Robusto flash library needs to be enabled."
#endif

#include <robusto_conductor.h>

static char *flash_log_prefix;

#define TEST_FILE "/spiffs/robusto_example.txt"
#define TEST_DATA "Hello SPIFF-world!"
void start_flash_example(char *_log_prefix)
{

#ifdef CONFIG_ROBUSTO_FLASH_EXAMPLE_SPIFFS

    char *data;
    rob_ret_val_t read_result = robusto_spiff_read(TEST_FILE, &data);
    if (read_result == ROB_FAIL)
    {
        rob_ret_val_t write_result = robusto_spiff_write(TEST_FILE, TEST_DATA, sizeof(TEST_DATA));
        if (write_result == ROB_FAIL)
        {
            ROB_LOGE(flash_log_prefix, "Failed writing to SPIFF-file");
            return;
        }
        else
        {
            read_result = robusto_spiff_read(TEST_FILE, &data);
        }
    }
    
    if (read_result == ROB_FAIL)
    {
        ROB_LOGE(flash_log_prefix, "Failed reading from %s", TEST_FILE);
        return;
    }
    else
    {
        if (strcmp(TEST_DATA, data) == 0)
        {
            ROB_LOGI(flash_log_prefix, "Read correct data from file");
        }
        else
        {
            ROB_LOGE(flash_log_prefix, "Data differed.\nTest data: %s\nRead data: %s", TEST_DATA, data);
        }
    }

#endif
}

void init_flash_example(char *_log_prefix)
{
    flash_log_prefix = _log_prefix;
}

#endif