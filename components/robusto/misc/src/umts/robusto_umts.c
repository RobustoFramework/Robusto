// TODO: The naming of things in the KConfig still have the names of the examples, this needs to change

#include "robusto_umts.h"

#ifdef CONFIG_ROBUSTO_UMTS_SERVER

#include "robusto_umts_def.h"

#include <string.h>

#include "robusto_logging.h"

#include "robusto_umts_task.h"
#include "robusto_umts_mqtt.h"

#include "robusto_conductor.h"

#include "robusto_umts_queue.h"

#include <esp_timer.h>
#include <esp_event.h>
#include <robusto_sleep.h>
#include <robusto_network_service.h>


char *umts_log_prefix;


bool successful_data = false;

RTC_DATA_ATTR uint connection_failures;
RTC_DATA_ATTR uint connection_successes;

EventGroupHandle_t umts_event_group = NULL;

void on_incoming(robusto_message_t *message);
void shutdown_utms_network_service(void);


char * hello_log_prefix;

network_service_t umts_network_service = {
    service_id : ROBUSTO_MQTT_SERVICE_ID,
    service_name : "UMTS",
    service_frees_message: true,
    incoming_callback : &on_incoming,
    shutdown_callback: &shutdown_utms_network_service
};

void shutdown_hello_service(void) {
}


void shutdown_utms_network_service(void) {

    ROB_LOGW(hello_log_prefix, "UMTS network service shutdown.");
}

bool shutdown_umts_network_service()
{
    if (!successful_data) {
        connection_failures++;
    }
    ROB_LOGI(umts_log_prefix, "----- Turning off all GSM/UMTS stuff -----");

    umts_cleanup();
    

  /*  ROB_LOGI(umts_log_prefix, "- Setting pin 12 (LED) to hight.");
    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT);
    gpio_pullup_en(GPIO_NUM_12);
    gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(200/portTICK_PERIOD_MS);
     gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(200/portTICK_PERIOD_MS);
     gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(200/portTICK_PERIOD_MS);
     gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(200/portTICK_PERIOD_MS);*/
    return true;
}

rob_ret_val_t robusto_umts_mqtt_publish(char * topic, char *data) {
    ROB_LOGI(umts_log_prefix, "Publishing %s to topic %s", data, topic);
    int res = umts_mqtt_publish(topic, data,  strlen(data));
    if (res == -1) {
        ROB_LOGI(umts_log_prefix, "Publishing failed.");    
        return ROB_FAIL;
    } else {
        ROB_LOGI(umts_log_prefix, "Publishing done, msg id %i.", res);
        return ROB_OK;
    }
    
}
void on_incoming(robusto_message_t *message) {
    umts_queue_item_t * new_q = robusto_malloc(sizeof(umts_queue_item_t));
    new_q->message = message;
    umts_safe_add_work_queue(new_q);
}

void umts_do_on_work_cb(umts_queue_item_t *queue_item) {

    //if (queue_item->message->service_id == ROBUSTO_MQTT_SERVICE_ID) 

    // TODO: Consider what actually is the point here, should GSM=MQTT?

    //ROB_LOGI(umts_log_prefix, "In UMTS work callback string %lu", (uint32_t)queue_item->message->raw_data);
    //rob_log_bit_mesh(ROB_LOG_INFO, umts_log_prefix, queue_item->message->raw_data, queue_item->message->raw_data_length);

    if ((strcmp(queue_item->message->strings[1], "-1.00") != 0) && (strcmp(queue_item->message->strings[1], "-2.00") != 0)) {
        umts_mqtt_publish("/topic/lurifax/peripheral_humidity", queue_item->message->strings[1],  strlen(queue_item->message->strings[1]));
        umts_mqtt_publish("/topic/lurifax/peripheral_temperature", queue_item->message->strings[2],  strlen(queue_item->message->strings[2]));
    }
    umts_mqtt_publish("/topic/lurifax/peripheral_since_wake", queue_item->message->strings[3],  strlen(queue_item->message->strings[3]));
    umts_mqtt_publish("/topic/lurifax/peripheral_since_boot", queue_item->message->strings[4],  strlen(queue_item->message->strings[4]));
    umts_mqtt_publish("/topic/lurifax/peripheral_free_mem", queue_item->message->strings[5],  strlen(queue_item->message->strings[5]));
    umts_mqtt_publish("/topic/lurifax/peripheral_total_wake_time", queue_item->message->strings[6],  strlen(queue_item->message->strings[6]));
    umts_mqtt_publish("/topic/lurifax/peripheral_voltage", queue_item->message->strings[7],  strlen(queue_item->message->strings[7]));
    umts_mqtt_publish("/topic/lurifax/peripheral_state_of_charge", queue_item->message->strings[8],  strlen(queue_item->message->strings[8]));
    umts_mqtt_publish("/topic/lurifax/peripheral_battery_current", queue_item->message->strings[9],  strlen(queue_item->message->strings[9]));
    umts_mqtt_publish("/topic/lurifax/peripheral_mid_point_voltage", queue_item->message->strings[10],  strlen(queue_item->message->strings[10]));


    char * curr_time;
    asprintf(&curr_time, "%.2f", (double)r_millis()/(double)1000);

    char * since_start;
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    asprintf(&since_start, "%.2f", (double)robusto_conductor_server_get_time_since_start()/(double)(1000));
    #else
    asprintf(&since_start, "N/A");
    #endif
    char * total_wake_time;
    asprintf(&total_wake_time, "%.2f", (double)robusto_get_total_time_awake()/(double)(1000));

    char * free_mem;
    asprintf(&free_mem, "%i", heap_caps_get_free_size(MALLOC_CAP_EXEC));

    char * sync_att;
    asprintf(&sync_att, "%i", get_sync_attempts());

    char * c_connection_failues;
    asprintf(&c_connection_failues, "%i", connection_failures);

    umts_mqtt_publish("/topic/lurifax/controller_since_wake", curr_time,  strlen(curr_time));
    umts_mqtt_publish("/topic/lurifax/controller_since_boot", since_start,  strlen(since_start));
    umts_mqtt_publish("/topic/lurifax/controller_total_wake_time", total_wake_time,  strlen(total_wake_time));    
    umts_mqtt_publish("/topic/lurifax/controller_free_mem", free_mem,  strlen(free_mem));    
    umts_mqtt_publish("/topic/lurifax/controller_sync_attempts", sync_att,  strlen(sync_att));   
    umts_mqtt_publish("/topic/lurifax/controller_connection_failures", c_connection_failues,  strlen(c_connection_failues)); 
    
    // Lets deem this a success.
    connection_successes++;
    char * c_connection_successes;
    asprintf(&c_connection_successes, "%i", connection_successes);
    umts_mqtt_publish("/topic/lurifax/controller_connection_successes", c_connection_successes,  strlen(c_connection_successes)); 
/*TODO: Fix this? 
    char * c_battery_voltage;
    asprintf(&c_battery_voltage, "%.2f", sdp_read_battery());
    umts_mqtt_publish("/topic/lurifax/controller_battery_voltage", c_battery_voltage,  strlen(c_battery_voltage)); 
*/
    umts_cleanup_queue_task(queue_item);
    successful_data = true;


}   

void umts_reset_rtc() {
    connection_failures = 0;
    connection_successes = 0;
}

void robusto_umts_init(char *_log_prefix)
{
    umts_log_prefix = _log_prefix;
    ROB_LOGI(umts_log_prefix, "Initiating UMTS modem..");

    // Keeping this here to inform that the event loop is created in sdp_init, not here
    // TODO: This is called here to be sure, possibly it should be called in some initialization instead. Or does it matter?
    esp_event_loop_create_default();

    /* Create the event group, this is used for all event handling, initiate in main thread */
    umts_event_group = xEventGroupCreate();
    xEventGroupClearBits(umts_event_group, GSM_CONNECT_BIT | GSM_GOT_DATA_BIT | GSM_SHUTTING_DOWN_BIT);

    umts_init_queue(&umts_do_on_work_cb, umts_log_prefix);
    robusto_register_network_service(&umts_network_service);

    ROB_LOGI(umts_log_prefix, "* Registering GSM main task...");
    int rc = xTaskCreatePinnedToCore((TaskFunction_t)robusto_umts_start, "UMTS main task", /*8192*/ /*16384*/ 32768, 
        (void *)umts_log_prefix, 5, &umts_modem_setup_task, 0);
    if (rc != pdPASS)
    {
        ROB_LOGE(umts_log_prefix, "Failed creating UMTS task, returned: %i (see projdefs.h)", rc);
    }


    ROB_LOGI(umts_log_prefix, "* UMTS main task registered.");
}

#endif
