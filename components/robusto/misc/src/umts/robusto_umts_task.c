#include "robusto_umts_task.h"
#ifdef CONFIG_ROBUSTO_UMTS_SERVER
#include <robusto_umts.h>
#include "robusto_umts_def.h"

#include <esp_modem_api.h>
#include "esp_modem_dce_config.h"
#include <driver/gpio.h>

#include "robusto_umts_ip.h"
#ifdef CONFIG_ROBUSTO_UMTS_HTTP
#include "robusto_umts_http.h"
#endif
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
#include "robusto_umts_mqtt.h"
#endif

#include "robusto_umts_queue.h"
#include "robusto_logging.h"

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
#include "robusto_conductor.h"
#endif

#include <string.h>

TaskHandle_t umts_modem_setup_task;
esp_modem_dce_t *umts_dce = NULL;
char *operator_name = NULL;

char *umts_task_log_prefix = NULL;
int sync_attempts = 0;

bool started = false;

int get_sync_attempts()
{
    return sync_attempts;
}

void robusto_umts_set_started(bool state) {
    started = state;
}

bool robusto_umts_get_started() {
    return started;
}

void cut_modem_power()
{
    // Short delay before cutting power.

    ROB_LOGI(umts_task_log_prefix, "* Cutting modem power.");
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    ROB_LOGI(umts_task_log_prefix, "- Setting pulldown mode. (float might be over 0.4 v)");
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLDOWN_ONLY);
    ROB_LOGI(umts_task_log_prefix, "- Setting pin 4 to high.");
    r_delay(1000);
    gpio_set_level(GPIO_NUM_4, 1);
    ROB_LOGI(umts_task_log_prefix, "- Setting pin 4 to low, wait 1000 ms.");
    gpio_set_level(GPIO_NUM_4, 0);
    r_delay(1000);
    ROB_LOGI(umts_task_log_prefix, "- Setting pin 4 to high again (shutdown may take up to 6.2 s).");
    gpio_set_level(GPIO_NUM_4, 1);
    r_delay(6200);

    ROB_LOGI(umts_task_log_prefix, "* Waiting...");
    r_delay(1000);
    /*
        ROB_LOGI(umts_task_log_prefix, "* Setting pin 4 to low again.");
        r_delay(1000/portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_4, 0);
        r_delay(1000/portTICK_PERIOD_MS);
        */
    ROB_LOGI(umts_task_log_prefix, "* Power cut.");

    // sdp_blink_led(GPIO_NUM_12, 100, 100, 10);
}

void umts_cleanup()
{
    /* As we clear umts_event_group in the end of this function, we'll use it to know if it's been run. */
    if (!umts_event_group)
    {
        ROB_LOGW(umts_task_log_prefix, "GSM cleanup called when no cleanup is necessary. Exiting.");
        return;
    }
    if (umts_dce)
    {
        ROB_LOGI(umts_task_log_prefix, "* Disconnecting the modem using ATH");
        char res[100];
        esp_err_t err = esp_modem_at(umts_dce, "AT+ATH", res, 10000);
        if (err != ESP_OK)
        {
            ROB_LOGI(umts_task_log_prefix, "Disconnecting the modem ATH succeeded, response: %s .", res);
        }
        else
        {
            ROB_LOGE(umts_task_log_prefix, "Disconnecting the modem ATH failed with %d", err);
        }
    }
    umts_shutdown_queue();

    ROB_LOGI(umts_task_log_prefix, " - Informing everyone that the GSM task it is shutting down.");
    xEventGroupSetBits(umts_event_group, GSM_SHUTTING_DOWN_BIT);
    // Wait for the event to propagate
    r_delay(400);

    if (umts_event_group)
    {

        ROB_LOGI(umts_task_log_prefix, " - Delete the event group");

        vEventGroupDelete(umts_event_group);
        umts_event_group = NULL;
    }

    umts_ip_cleanup();

    r_delay(1200);
    // Making sure umts_modem_setup_task is null and use a temporary variable instead.
    // This to avoid a race condition, as umts_before_sleep_cb may be called simultaneously

    if (umts_modem_setup_task)
    {
        ROB_LOGI(umts_task_log_prefix, " - Deleting GSM setup task.");
        vTaskDelete(umts_modem_setup_task);
    }

    if (umts_dce)
    {

        ROB_LOGI(umts_task_log_prefix, "* Powering down GPS by turning off power line (SGPIO=0,4,1,0)");
        char res[100];
        esp_err_t err = esp_modem_at(umts_dce, "AT+SGPIO=0,4,1,0", res, 10000);
        if (err != ESP_OK)
        {
            ROB_LOGI(umts_task_log_prefix, "Powering down GPS by turning off power line succeeded, response: %s .", res);
        }
        else
        {
            ROB_LOGE(umts_task_log_prefix, "Powering down GPS using AT failed with %d", err);
        }
        /*
        ROB_LOGI(umts_task_log_prefix, " - Set command mode");
        esp_err_t err = esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_COMMAND);
        if (err != ESP_OK)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_COMMAND) failed with %d", err);
        }
        */
        /*
        ROB_LOGI(umts_task_log_prefix, "* Sending SMS report after MQTT");
        err = esp_modem_send_sms(umts_dce, "", "Going to sleep, all successful.");
        if (err != ESP_OK)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_send_sms(); failed with error:  %i", err);
        } else {

            r_delay(4000/portTICK_PERIOD_MS);
        }
        */
        /*
        ROB_LOGI(umts_task_log_prefix, " - De-register from GSM/UMTS network.");
         err = esp_modem_set_operator(umts_dce,  3, 1, operator_name);
        if (err != ESP_OK)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_set_operator(umts_dce,  3, 1, %s) failed with %d",operator_name, err);
        }

        // Power off the modem AT command.
        ROB_LOGI(umts_task_log_prefix, " - Tell modem to power down");
        esp_err_t err = esp_modem_power_down(umts_dce);
        if (err == ESP_ERR_TIMEOUT)
        {
            ROB_LOGW(umts_task_log_prefix, "esp_modem_power_down(umts_dce) timed out");
        } else
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_power_down(umts_dce) failed with %d", err);
        }

        ROB_LOGI(umts_task_log_prefix, "* Powering down GPS using CGNSPWR=0");
        char res[100];
        esp_err_t  err = esp_modem_at(umts_dce, "AT+CGNSPWR=0", res, 10000);
        if (err != ESP_OK)
        {
            ROB_LOGI(umts_task_log_prefix, "Powering down GPS using AT succeeded, response: %s .", res);
        } else
        {
            ROB_LOGE(umts_task_log_prefix, "Powering down GPS using AT failed with %d", err);
        }
        ROB_LOGI(umts_task_log_prefix, "* Done powering down using CGNSPWR=0");
        */
        /*


         err = esp_modem_at(umts_dce, "AT+CPOWD=1", res, 10000);
         if (err != ESP_OK)  a
         {
             ROB_LOGI(umts_task_log_prefix, " - Powering down using AT succeeded, response: %s .", res);
         } else
         {
             ROB_LOGE(umts_task_log_prefix, "Powering down using AT failed with %d", err);
         }
  */
        /*
        ROB_LOGI(umts_task_log_prefix, "- Destroying esp-modem");
        esp_modem_destroy(umts_dce);
        */
    }

    r_delay(1000);
    cut_modem_power();
}

rob_ret_val_t robusto_umts_sms_send(const char *number, const char *message_string)
{
    // TODO: Use the working queue for the UMTS stuff to move between data mode
    if (!umts_dce)
    {
        ROB_LOGE(umts_task_log_prefix, "esp_modem_send_sms(); modem not initiated.");
        return ROB_FAIL;
    }

    if (esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_COMMAND) != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "Setting command mode failed");
        return ROB_FAIL;
    }
    if (esp_modem_sms_txt_mode(umts_dce, true) != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "Setting text mode failed");
        return ROB_FAIL;
    }
    if (esp_modem_sms_character_set(umts_dce) != ESP_OK) {
        ROB_LOGE(umts_task_log_prefix, "Setting GSM character set failed");
        return ROB_FAIL;
    }
    ROB_LOGI(umts_task_log_prefix, "Sending SMS to %s, message: \"%s\"", number, message_string);
    esp_err_t err = esp_modem_send_sms(umts_dce, number, message_string);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "esp_modem_send_sms(); failed with error:  %i", err);
        
        esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_DATA);
    
        return ROB_FAIL;
    }
    else
    {
        esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_DATA);
        return ROB_OK;
    }
}
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
void umts_abort_if_shutting_down()
{
    if ((umts_event_group == NULL) || (xEventGroupGetBits(umts_event_group) & GSM_SHUTTING_DOWN_BIT))
    {
        ROB_LOGW(umts_task_log_prefix, "GSM start: Told that we are shutting down; exiting. ");
        umts_modem_setup_task = NULL;
        vTaskDelete(NULL);
    }
}
#endif
void handle_umts_states(int state)
{
    if (state == -GSM_SHUTTING_DOWN_BIT)
    {

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
        umts_abort_if_shutting_down();
#endif
    }
}

bool robusto_umts_sms_up()
{
    return (umts_dce != NULL) && started;
}




void robusto_umts_start(char *_log_prefix)
{

    started = false;
    umts_modem_setup_task = NULL;
    sync_attempts = 0;

    umts_task_log_prefix = _log_prefix;
    operator_name = malloc(64);

    /* Configure the PPP netif */ 
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_ROBUSTO_UMTS_MODEM_PPP_APN);

    // We need to init the PPP netif as that is a parameter to the modem setup
    umts_ip_init(umts_task_log_prefix);
    #ifdef CONFIG_ROBUSTO_UMTS_HTTP
    umts_http_init(umts_task_log_prefix);
    #endif
    ROB_LOGI(umts_task_log_prefix, "Powering on modem.");
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    ROB_LOGI(umts_task_log_prefix, " + Setting pulldown mode. (float might be over 0.4 v)");
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLDOWN_ONLY);
    ROB_LOGI(umts_task_log_prefix, " + Setting PWRKEY high");
    gpio_set_level(GPIO_NUM_4, 1);
    ROB_LOGI(umts_task_log_prefix, " + Setting PWRKEY low");
    gpio_set_level(GPIO_NUM_4, 0);
    ROB_LOGI(umts_task_log_prefix, " + Waiting 1 second.");
    r_delay(1000);
    ROB_LOGI(umts_task_log_prefix, "+ Setting PWRKEY high");
    gpio_set_level(GPIO_NUM_4, 1);
    ROB_LOGI(umts_task_log_prefix, " + Setting DTR to high.");
    gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_25, 1);
    r_delay(2000);
    ROB_LOGI(umts_task_log_prefix, " + Setting DTR to low.");
    gpio_set_level(GPIO_NUM_25, 0);
    ROB_LOGI(umts_task_log_prefix, "+ Waiting 2 seconds.");
    r_delay(2000);
    /* Configure the DTE */
#if defined(CONFIG_ROBUSTO_UMTS_SERIAL_CONFIG_UART)
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    /* setup UART specific configuration based on kconfig options */
    dte_config.uart_config.baud_rate = 115200;
    dte_config.uart_config.tx_io_num = CONFIG_ROBUSTO_UMTS_MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = CONFIG_ROBUSTO_UMTS_MODEM_UART_RX_PIN;
    dte_config.uart_config.rts_io_num = -1; // CONFIG_ROBUSTO_UMTS_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = -1; // CONFIG_ROBUSTO_UMTS_MODEM_UART_CTS_PIN;

    dte_config.uart_config.rx_buffer_size = CONFIG_ROBUSTO_UMTS_MODEM_UART_RX_BUFFER_SIZE;
    dte_config.uart_config.tx_buffer_size = CONFIG_ROBUSTO_UMTS_MODEM_UART_TX_BUFFER_SIZE;
    dte_config.uart_config.event_queue_size = CONFIG_ROBUSTO_UMTS_MODEM_UART_EVENT_QUEUE_SIZE;
    dte_config.task_stack_size = CONFIG_ROBUSTO_UMTS_MODEM_UART_EVENT_TASK_STACK_SIZE;
    dte_config.task_priority = CONFIG_ROBUSTO_UMTS_MODEM_UART_EVENT_TASK_PRIORITY;
    dte_config.dte_buffer_size = CONFIG_ROBUSTO_UMTS_MODEM_UART_RX_BUFFER_SIZE;
    

#if CONFIG_ROBUSTO_UMTS_MODEM_DEVICE_BG96 == 1
    ROB_LOGI(umts_task_log_prefix, "Initializing esp_modem for the BG96 module...");
    umts_dce = esp_modem_new_dev(ESP_MODEM_DCE_BG96, &dte_config, &dce_config, umts_ip_esp_netif);
#elif CONFIG_ROBUSTO_UMTS_MODEM_DEVICE_SIM800 == 1
    ROB_LOGI(umts_task_log_prefix, "Initializing esp_modem for the SIM800 module...");
    umts_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, umts_ip_esp_netif);
#elif CONFIG_ROBUSTO_UMTS_MODEM_DEVICE_SIM7600 == 1
    ROB_LOGI(umts_task_log_prefix, "Initializing esp_modem for the SIM7600 module...");
    umts_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, umts_ip_esp_netif);
#elif CONFIG_ROBUSTO_UMTS_MODEM_DEVICE_SIM7000 == 1

    ROB_LOGI(umts_task_log_prefix, "Initializing esp_modem for the SIM7000 module..umts_ip_esp_netif assigned %p", umts_ip_esp_netif);
    umts_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, umts_ip_esp_netif);
    assert(umts_dce);

#else
    ROB_LOGI(umts_task_log_prefix, "Initializing esp_modem for a generic module...");
    umts_dce = esp_modem_new(&dte_config, &dce_config, umts_ip_esp_netif);
#endif

#endif
/*For some reason the initial startup may take long */
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_ask_for_time(5000);
#endif

    char res[100];
    esp_err_t err = ESP_FAIL;
    ROB_LOGI(umts_task_log_prefix, " - Syncing with the modem (AT)...");
    while (err != ESP_OK)
    {

        err = esp_modem_sync(umts_dce); //, "AT", res, 4000);
        if (err != ESP_OK)
        {
            sync_attempts++;

            if (err == ESP_ERR_TIMEOUT)
            {
                ROB_LOGI(umts_task_log_prefix, "Sync attempt %i timed out.", sync_attempts);
            }
            else
            {
                ROB_LOGE(umts_task_log_prefix, "Sync attempt %i failed with error:  %i", sync_attempts, err);
            }

// We want to try until we either connect or hit the timebox limit
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
            umts_abort_if_shutting_down();
            robusto_conductor_server_ask_for_time(100);
#endif
            r_delay(500);
        }
        else
        {
            ROB_LOGI(umts_task_log_prefix, "Sync returned:  %s", res);
        }
    }

/* We are now much more likely to be able to connect, ask for 7.5 more seconds for the next phase */
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    umts_abort_if_shutting_down();
    robusto_conductor_server_ask_for_time(5000);
#endif

    ROB_LOGI(umts_task_log_prefix, "* Preferred mode selection; Automatic");

    err = esp_modem_at(umts_dce, "AT+CNMP=38", &res, 15000);
    if (err != ESP_OK)
    {
        ROB_LOGW(umts_task_log_prefix, "esp_modem_at CNMP=38 failed with error:  %i", err);
    }
    else
    {
        ROB_LOGI(umts_task_log_prefix, "CNMP=38 returned:  %s", res);
    }
#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    umts_abort_if_shutting_down();
    robusto_conductor_server_ask_for_time(15500);
#endif

    ROB_LOGI(umts_task_log_prefix, "* Preferred selection between CAT-M and NB-IoT");
    err = esp_modem_at(umts_dce, "AT+CMNB=1", &res, 15000);
    if (err != ESP_OK)
    {
        ROB_LOGW(umts_task_log_prefix, "esp_modem_at CMNB=1 failed with error:  %i", err);
        // TODO: This is probably fatal
    }
    else
    {
        ROB_LOGI(umts_task_log_prefix, "AT+CMNB=1 returned:  %s", res);
        r_delay(1000);
    }

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    umts_abort_if_shutting_down();
    robusto_conductor_server_ask_for_time(500);
#endif

    /*ROB_LOGE(umts_task_log_prefix, "Modem synced. resetting");
    char res[100];
    err = esp_modem_at(umts_dce, "AT+ATZ", &res, 1000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "esp_modem_reset failed with error:  %i", err);
    }
    else
    {
        r_delay(1000);
    }*/

    /* Run the modem demo app */
#if CONFIG_ROBUSTO_UMTS_NEED_SIM_PIN == 1
    // check if PIN needed
    bool pin_ok = false;
    if (esp_modem_read_pin(umts_dce, &pin_ok) == ESP_OK && pin_ok == false)
    {
        if (esp_modem_set_pin(umts_dce, CONFIG_ROBUSTO_UMTS_SIM_PIN) == ESP_OK)
        {
            r_delay(1000);
        }
        else
        {
            abort();
        }
    }
#endif

    int rssi, ber;
    int retries = 0;
signal_quality:

    ROB_LOGI(umts_task_log_prefix, "* Checking for signal quality..");
    err = esp_modem_get_signal_quality(umts_dce, &rssi, &ber);

    if (err != ESP_OK)
    {
        ROB_LOGW(umts_task_log_prefix, "esp_modem_get_signal_quality failed with error:  %i", err);
        umts_cleanup();
    }
    if (rssi == 99)
    {
        r_delay(1000);
        retries++;
        if (retries > 15)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_get_signal_quality returned 99 (Not known/connected) for rssi after 15 retries.");
            ROB_LOGE(umts_task_log_prefix, "It seems we don't have a proper connection, quitting (TODO: troubleshoot).");
            goto finish;
        }
        else
        {
            ROB_LOGI(umts_task_log_prefix, " esp_modem_get_signal_quality returned 99 (Not known/connected) for rssi, trying again");
            goto signal_quality;
        }
    }
    ROB_LOGI(umts_task_log_prefix, "* Signal quality: rssi=%d, ber=%d", rssi, ber);

    ROB_LOGI(umts_task_log_prefix, "Checking registration.");
    // char res[100];
    bool registered = false;
    err = esp_modem_at(umts_dce, "AT+CREG?", &res, 20000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "esp_modem_at CREG failed with error:  %i", err);
    }
    else
    {
        ROB_LOGI(umts_task_log_prefix, "CREG? returned:  %s", res);
        if (strcmp(res, "+CREG: 0,2") == 0)
        {
            registered = false;
        }
        else
        {
            registered = true;
        }
    }

    if (!registered)
    {
        ROB_LOGW(umts_task_log_prefix, "We are not registered, start registering using AT+COPS=0 (auto select operator).");
        err = esp_modem_at(umts_dce, "AT+COPS=0", &res, 120000);
        if (err != ESP_OK)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_at AT+COPS=0 failed with error:  %i", err);
        }
        else
        {
            ROB_LOGI(umts_task_log_prefix, "AT+COPS=0 returned:  %s", res);
            r_delay(6000);
        }
    }
    
    while (!registered)
    {
        err = esp_modem_at(umts_dce, "AT+CREG?", &res, 20000);
        if (err != ESP_OK)
        {
            ROB_LOGE(umts_task_log_prefix, "esp_modem_at CREG failed with error:  %i", err);
        }
        else
        {
            ROB_LOGI(umts_task_log_prefix, "CREG? returned:  %s", res);
            if (strcmp(res, "+CREG: 0,2") == 0)
            {
                registered = false;
                r_delay(6000);
            }
            else
            {
                registered = true;
            }
        }
    }
    
    int act = 0;
    err = esp_modem_get_operator_name(umts_dce, operator_name, &act);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_task_log_prefix, "esp_modem_get_operator_name(umts_dce) failed with %d, is the SIM valid/paid?", err);
    }
    else
    {
        ROB_LOGI(umts_task_log_prefix, " + Operator name : %s, act: %i", operator_name, act);
    }

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_ask_for_time(500);
    umts_abort_if_shutting_down();
    robusto_conductor_server_ask_for_time(10000);
#endif
    // Change to data mode
    handle_umts_states(umts_ip_enable_data_mode());

#ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_ask_for_time(500);
#endif
    // Initialize MQTT
    
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
    handle_umts_states(umts_mqtt_init(umts_task_log_prefix));
#endif  
    

finish:
    
    // End init task
    vTaskDelete(NULL);
}
#endif