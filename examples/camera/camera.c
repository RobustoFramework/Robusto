#include "camera.h"
#ifdef CONFIG_ROBUSTO_CAMERA_EXAMPLE
#include "esp_camera.h"

#include <robusto_umts.h>
#include <robusto_logging.h>

#ifdef CONFIG_ROBUSTO_CAMERA
#include <robusto_camera.h>
#endif

char * sms_log_prefix;


void start_camera_example(char * _log_prefix)
{

    r_delay(30000);
    ROB_LOGI(sms_log_prefix, "Done camera stuff.");


}


void init_camera_example(char * _log_prefix) {
    sms_log_prefix = _log_prefix;
    
}


#endif