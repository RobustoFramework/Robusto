#include "sms.h"
#ifdef CONFIG_ROBUSTO_EXAMPLE_SMS


#include <robusto_umts.h>
#include <robusto_logging.h>

char * sms_log_prefix;

#if !defined(CONFIG_ROBUSTO_EXAMPLE_SMS_NUMBER) || !defined(CONFIG_ROBUSTO_EXAMPLE_SMS_MESSAGE)
#error "Both a SMS recipient number and a message must be set."
#endif

void start_sms_example(char * _log_prefix)
{

    
    while (!robusto_umts_base_up()) {
        ROB_LOGI(sms_log_prefix, "Waiting for device to get going");
        r_delay(1000);
    }
    ROB_LOGI(sms_log_prefix, "Trying to send an SMS.");
    robusto_umts_send_sms(CONFIG_ROBUSTO_EXAMPLE_SMS_NUMBER, CONFIG_ROBUSTO_EXAMPLE_SMS_MESSAGE);
}

void init_sms_example(char * _log_prefix) {
    sms_log_prefix = _log_prefix;
    robusto_umts_init(_log_prefix);

}


#endif