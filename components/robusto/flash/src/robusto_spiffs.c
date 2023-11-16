#include "../include/robusto_flash_init.h"
#include <robusto_flash.h>
#ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
#include <esp_spiffs.h>
#include <esp_vfs.h>
#include <fcntl.h>

#include <robusto_logging.h>
#include <robusto_system.h>
#define CHUNK_SIZE 1024
char *spiffs_log_prefix;

unsigned long get_file_size(int f)
{

    struct stat st;
    int fstat_res = fstat(f, &st);
    if (fstat_res == 0)
    {
        ROB_LOGI(spiffs_log_prefix, "File size of %li bytes\n", st.st_size);
        return st.st_size;
    }
    else
    {
        ROB_LOGE(spiffs_log_prefix, "Error getting file size: %i", fstat_res);
        return -1;
    }
}

rob_ret_val_t robusto_spiff_write(char *filename, char *data, uint16_t data_len)
{
    // Use POSIX and C standard library functions to work with files.
    // First create a file.

    ROB_LOGI(spiffs_log_prefix, "Opening file %s for writing", filename);
    int f = open(filename, S_IWRITE);
    if (f == 0)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to open file for writing");
        return ROB_FAIL;
    }
    write(f, data, data_len);
    close(f);
    return ROB_OK;
}

rob_ret_val_t robusto_spiff_read(char *filename, char **buffer)
{
    // Open renamed file for reading
    ROB_LOGI(spiffs_log_prefix, "Opening file %s for reading", filename);

    int f = open(filename, S_IREAD);
    if (!f)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to open file for reading");
        return ROB_FAIL;
    }
    unsigned long filesize = get_file_size(f);
    if (filesize == -1)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to get filesize");
        return ROB_FAIL;
    }
    // Use SPIRam if it is a large file
    if (filesize < 1000)
    {
        *buffer = robusto_malloc(filesize);
    }
    else
    {
        // TODO: Implement using SPIRam if availalbe.
        ROB_LOGI(spiffs_log_prefix, "File %s is too big, cannot read.", filename);
        return ROB_FAIL;
    }

    int bytesRead = 0;
    bytesRead = read(f, *buffer, filesize);
    close(f);
    return ROB_OK;
}

void robusto_spiffs_init(char *_log_prefix)
{

    spiffs_log_prefix = _log_prefix;

    ROB_LOGI(spiffs_log_prefix, "Mounting SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_ROBUSTO_FLASH_SPIFFS_PATH,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ROB_LOGE(spiffs_log_prefix, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ROB_LOGE(spiffs_log_prefix, "Failed to find SPIFFS partition");
        }
        else
        {
            ROB_LOGE(spiffs_log_prefix, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

#ifdef CONFIG_EXAMPLE_SPIFFS_CHECK_ON_START
    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK)
    {
        ESP_LOGE(spiffs_log_prefix, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    }
    else
    {
        ESP_LOGI(spiffs_log_prefix, "SPIFFS_check() successful");
    }
#endif

    // TODO: Perhaps this should be a setting. Is flash data very important
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
        return;
    }
    else
    {
        ROB_LOGI(spiffs_log_prefix, "Partition size: total: %d, used: %d", total, used);
    }

    // Check consistency of reported partiton size info.
    if (used > total)
    {
        ROB_LOGW(spiffs_log_prefix, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(conf.partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK)
        {
            ROB_LOGE(spiffs_log_prefix, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            return;
        }
        else
        {
            ROB_LOGI(spiffs_log_prefix, "SPIFFS_check() successful");
        }
    }
}

#endif