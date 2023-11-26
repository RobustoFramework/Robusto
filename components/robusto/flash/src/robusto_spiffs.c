#include "../include/robusto_flash_init.h"
#include <robusto_flash.h>
#ifdef CONFIG_ROBUSTO_FLASH_SPIFFS
#include <esp_spiffs.h>
#include <esp_vfs.h>
#include <sys/stat.h>
#include <errno.h>
#include <robusto_logging.h>
#include <robusto_system.h>
#define CHUNK_SIZE 1024
static char *spiffs_log_prefix;

unsigned long get_file_size(char * filename)
{
    ROB_LOGI(spiffs_log_prefix, "Getting file size of file %s.", filename);
    struct stat st;
    int fstat_res = stat(filename, &st);
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
    // Create a file

    ROB_LOGI(spiffs_log_prefix, "Opening file %s for writing %i bytes", filename, data_len);

    FILE* f = fopen(filename, "w");
    if (f == NULL) {
        ROB_LOGE(spiffs_log_prefix, "Failed to open file for writing, errno %i", errno);
        fclose(f);
        return ROB_FAIL;
    }
    fwrite(data, data_len, 1, f);
    fclose(f);
    ROB_LOGI(spiffs_log_prefix, "Wrote to file %s", filename);
    return ROB_OK;
}

rob_ret_val_t robusto_spiff_read(char *filename, char **buffer)
{
    // Open renamed file for reading
    ROB_LOGI(spiffs_log_prefix, "Opening file %s for reading", filename);

    FILE * f = fopen(filename, "r");
    if (f == NULL)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to open file for reading, errno %i", errno);
        fclose(f);
        return ROB_FAIL;
    }
    unsigned long filesize = get_file_size(filename);
    if (filesize < 1)
    {
        ROB_LOGE(spiffs_log_prefix, "Failed to get filesize or zero-length file (which isn't handle well here, )");
        fclose(f);
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
        fclose(f);
        return ROB_FAIL;
    }

    int bytesRead = 0;
    bytesRead = fread(*buffer, filesize, 1, f);
    fclose(f);
    return ROB_OK;
}

rob_ret_val_t robusto_spiff_remove(char *filename) {
    // Open file for deletion
    ROB_LOGI(spiffs_log_prefix, "Opening file %s for reading", filename);
    if (remove(filename) != 0) {
        ROB_LOGE(spiffs_log_prefix, "Failed to remove file, errno %i", errno);
        return ROB_FAIL;
    } else {
        return ROB_OK;
    }
}

void robusto_spiffs_init(char *_log_prefix)
{

    spiffs_log_prefix = _log_prefix;


    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_ROBUSTO_FLASH_SPIFFS_PATH,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ROB_LOGI(spiffs_log_prefix, "Mounting the first SPIFFS partition at %s", conf.base_path);

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
    ROB_LOGI(spiffs_log_prefix, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK)
    {
        ROB_LOGE(spiffs_log_prefix, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    }
    else
    {
        ROB_LOGI(spiffs_log_prefix, "SPIFFS_check() successful");
    }
#endif
    //    ROB_LOGE(spiffs_log_prefix, "Formatting SPIFFS...");
    //    esp_spiffs_format(conf.partition_label);
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