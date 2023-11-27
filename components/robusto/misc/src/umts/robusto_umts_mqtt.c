/**
 * @file gsm_mqtt.c
 * @author your name (you@domain.com)
 * @brief Usage of the ESP-IDF MQTT client. 
 * TODO: This should probably be separate, able to also use wifi or any other channel.
 * @version 0.1
 * @date 2022-11-20
 * 
 * @copyright Copyright (c) 2022
 * 
 */


#include "robusto_umts_mqtt.h"
#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
#include "mqtt_client.h"

#include "robusto_sleep.h"
#include "robusto_conductor.h"
#include "robusto_logging.h"
#include "robusto_umts_def.h"
#include "robusto_umts_task.h"
#include "robusto_umts_queue.h"


//#define TOPIC "/topic/robusto_subscription"

char *umts_mqtt_log_prefix = "mqtt log prefix not set";

esp_mqtt_client_handle_t mqtt_client = NULL;

ROB_RTC_DATA_ATTR int mqtt_count;
bool mqtt_up = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ROB_LOGD(umts_mqtt_log_prefix, "Event dispatched from event loop base=%s, event_id=%li", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_CONNECTED");
        // TODO: Implement subscription handling
        //msg_id = esp_mqtt_client_subscribe(client, "/topic/lurifax_test", 0);
        // All is initiated, we can now start handling the queue
        umts_set_queue_blocked(false);
        mqtt_up = true;
        robusto_umts_set_started(true);
        //ROB_LOGI(umts_mqtt_log_prefix, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        umts_set_queue_blocked(true);
        ROB_LOGW(umts_mqtt_log_prefix, "MQTT_EVENT_DISCONNECTED");
        mqtt_up = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
        robusto_conductor_server_ask_for_time(5000000); 
        #endif
        //msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", "esp32-pppos", 0, 0, 0);
        //ROB_LOGI(umts_mqtt_log_prefix, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        mqtt_count++;    
        break;
    case MQTT_EVENT_DATA:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        xEventGroupSetBits(umts_event_group, GSM_GOT_DATA_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT_EVENT_BEFORE_CONNECT");
        break;   
    default:
        ROB_LOGI(umts_mqtt_log_prefix, "MQTT other event id: %i", event->event_id);
        break;
    }
}


bool robusto_umts_mqtt_up()
{
    return mqtt_up && robusto_umts_get_started();
}

void umts_mqtt_cleanup() {
    if (mqtt_client) {

        ROB_LOGI(umts_mqtt_log_prefix, "* MQTT shutting down.");
        ROB_LOGI(umts_mqtt_log_prefix, " - Success in %i of %i of sleep cycles.", mqtt_count, robusto_get_sleep_count());
        // TODO: Subscription
        //ROB_LOGI(umts_mqtt_log_prefix, " - Unsubscribing the client from the %s topic.", TOPIC);
        //esp_mqtt_client_unsubscribe(mqtt_client, TOPIC);
        ROB_LOGI(umts_mqtt_log_prefix, " - Destroying the client.");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    } else {
        ROB_LOGI(umts_mqtt_log_prefix, "* MQTT already shut down and cleaned up or not started, doing nothing.");
    }


}

int umts_mqtt_publish(char * topic, char * payload, int payload_len) {
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, payload_len, 0, 1);
    if (msg_id == -1) {
        ROB_LOGE(umts_mqtt_log_prefix, "Failed to publish data (msg id -1).");    
    } else
    if (msg_id == -2) {
        ROB_LOGE(umts_mqtt_log_prefix, "Failed to publish data, outbox full. (msg id -1).");    
    } else {
        ROB_LOGI(umts_mqtt_log_prefix, "Publishing \"%s\" to %s. Msg id %i.", payload, topic, msg_id);    
    }

    return msg_id;
}



int umts_mqtt_init(char * _log_prefix) {
    
    umts_mqtt_log_prefix = _log_prefix;

    if (robusto_is_first_boot()) {
        mqtt_count = 0;
    }

/* Config MQTT */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY_BROKER_URL,
    };
#else
    esp_mqtt_client_config_t mqtt_config = {
        .uri = CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY_BROKER_URL,

    };
#endif
    ROB_LOGI(umts_mqtt_log_prefix, "* Start MQTT client:");
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ROB_LOGI(umts_mqtt_log_prefix, " + Register callbacks");
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL); 
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    umts_abort_if_shutting_down();
    robusto_conductor_server_ask_for_time(5000);
    #endif
    ROB_LOGI(umts_mqtt_log_prefix, " + Start the client");
    esp_mqtt_client_start(mqtt_client);

    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    umts_abort_if_shutting_down();
    #endif
    // TODO: Enable subscription
    //ROB_LOGI(umts_mqtt_log_prefix, " + Subscribe to the the client from the %s topic.", TOPIC);
    //esp_mqtt_client_subscribe(mqtt_client, TOPIC, 1);
    ROB_LOGI(umts_mqtt_log_prefix, "* MQTT Running.");
    //TODO:Move the following to a task
    #if 0
    ROB_LOGI(umts_mqtt_log_prefix, "Waiting for MQTT data");
    EventBits_t uxBits = xEventGroupWaitBits(umts_event_group, GSM_GOT_DATA_BIT | GSM_SHUTTING_DOWN_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if ((uxBits & GSM_SHUTTING_DOWN_BIT) != 0)
    {
        ROB_LOGE(umts_mqtt_log_prefix, "Getting that we are shutting down, pausing indefinitely.");
        vTaskDelay(portMAX_DELAY);
    }
    
    ROB_LOGI(umts_mqtt_log_prefix, "Has MQTT data");


    mqtt_count++;    
    #endif
    return ESP_OK;
}
#endif