// TODO: The naming of things in the KConfig still have the names of the examples, this needs to change

#include "robusto_umts.h"

#ifdef CONFIG_ROBUSTO_UMTS_SERVER

#include "robusto_umts_def.h"

#include <string.h>

#include "robusto_logging.h"

#include "robusto_umts_task.h"
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
#include "robusto_umts_mqtt.h"
#endif

#include "robusto_conductor.h"

#include "robusto_umts_queue.h"

#include <esp_timer.h>
#include <esp_event.h>
#include <robusto_sleep.h>
#include <robusto_network_service.h>

static char *umts_log_prefix;

bool successful_data = false;

ROB_RTC_DATA_ATTR unsigned int connection_failures;
ROB_RTC_DATA_ATTR unsigned int connection_successes;

EventGroupHandle_t umts_event_group = NULL;


unsigned int get_connection_failures(){
    return connection_failures;
}
unsigned int get_connection_successes(){
    return connection_successes;
}

bool shutdown_umts_network_service()
{
    if (!successful_data)
    {
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
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
rob_ret_val_t robusto_umts_mqtt_publish(char *topic, char *data)
{
    ROB_LOGI(umts_log_prefix, "Publishing %s to topic %s", data, topic);

    umts_queue_item_t *new_q = robusto_malloc(sizeof(umts_queue_item_t));
    new_q->topic = topic;
    new_q->data = data;
    rob_ret_val_t res = umts_safe_add_work_queue(new_q);
    if (res == ROB_OK)
    {
        ROB_LOGI(umts_log_prefix, "Added to MQTT work queue.");
        return ROB_OK;
    }
    else
    {
        ROB_LOGI(umts_log_prefix, "Fail adding it to the work queue, result code %i.", res);
        return ROB_FAIL;
    }
}
#endif
static void on_incoming(robusto_message_t *message)
{
    if (message->string_count == 2)
    {
        umts_queue_item_t *new_q = robusto_malloc(sizeof(umts_queue_item_t));
        new_q->topic = message->strings[0];
        new_q->data = message->strings[1];
        umts_safe_add_work_queue(new_q);
    }
    else
    {
        ROB_LOGE(umts_log_prefix, "UMTS on_incoming, invalid string count: %i", message->string_count);
    }
}

void umts_do_on_work_cb(umts_queue_item_t *queue_item)
{
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
    umts_mqtt_publish(queue_item->topic, queue_item->data, strlen(queue_item->data));

// if (queue_item->message->service_id == ROBUSTO_MQTT_SERVICE_ID)

// TODO: Consider what actually is the point here, should GSM=MQTT?

// ROB_LOGI(umts_log_prefix, "In UMTS work callback string %lu", (uint32_t)queue_item->message->raw_data);
// rob_log_bit_mesh(ROB_LOG_INFO, umts_log_prefix, queue_item->message->raw_data, queue_item->message->raw_data_length);
#endif
    umts_cleanup_queue_task(queue_item);
    successful_data = true;

}

void umts_reset_rtc()
{
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
