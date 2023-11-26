#include "robusto_umts_ip.h"
#ifdef CONFIG_ROBUSTO_UMTS_SERVER
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <esp_netif.h>
#include <esp_netif_ppp.h>

#include <esp_modem_api.h>
#include <esp_event.h>

#ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
#include "robusto_umts_mqtt.h"
#endif


#include "robusto_logging.h"

#include "robusto_umts_queue.h"
#include "inttypes.h"

#include "robusto_umts_def.h"
#include <robusto_conductor.h>


static char *umts_ip_log_prefix;
esp_netif_t *umts_ip_esp_netif = NULL;

static char * ip_addr_string = NULL;

char * get_ip_address() {
    if (ip_addr_string) {
        return ip_addr_string;
    } else {
        return "";
    }
    
}

bool robusto_umts_ip_up() {
    return (ip_addr_string != NULL);
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    ROB_LOGI(umts_ip_log_prefix, "PPP state changed event %li", event_id);
    if (event_id == NETIF_PPP_ERRORUSER)
    {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ROB_LOGW(umts_ip_log_prefix, "User interrupted event from netif:%p", netif);
    }
    else
    {
        ROB_LOGW(umts_ip_log_prefix, "Got Uncategorized ppp event! %li", event_id);
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ROB_LOGD(umts_ip_log_prefix, "IP event! %li", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP)
    {
        ROB_LOGI(umts_ip_log_prefix, "* GOT ip event!!!");
        
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ROB_LOGI(umts_ip_log_prefix, "Modem Connect to PPP Server");
        ROB_LOGI(umts_ip_log_prefix, "~~~~~~~~~~~~~~");
        ROB_LOGI(umts_ip_log_prefix, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ROB_LOGI(umts_ip_log_prefix, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ROB_LOGI(umts_ip_log_prefix, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ROB_LOGI(umts_ip_log_prefix, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ROB_LOGI(umts_ip_log_prefix, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ROB_LOGI(umts_ip_log_prefix, "~~~~~~~~~~~~~~");
        r_delay(1000);
        ip_addr_string = robusto_malloc(20);
        sprintf(ip_addr_string,  IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(umts_event_group, GSM_CONNECT_BIT);
       
    }
    else if (event_id == IP_EVENT_PPP_LOST_IP)
    {
        ROB_LOGW(umts_ip_log_prefix, "Modem Disconnect from PPP Server");
        robusto_free(ip_addr_string);
        ip_addr_string = NULL;
    }
    else if (event_id == IP_EVENT_GOT_IP6)
    {
        ROB_LOGI(umts_ip_log_prefix, "Got IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ROB_LOGI(umts_ip_log_prefix, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
    else
    {
        ROB_LOGW(umts_ip_log_prefix, "Got Uncategorized IP event! %li", event_id);
    }
}

void umts_ip_cleanup() {

    if (umts_ip_esp_netif) {
        #ifdef CONFIG_ROBUSTO_UMTS_MQTT_GATEWAY
        umts_mqtt_cleanup();
        #endif
        ROB_LOGI(umts_ip_log_prefix, "Cleaning up GSM IP");
      

        ROB_LOGI(umts_ip_log_prefix, "- Unregistering IP/Netif events.");
        esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event);
        esp_event_handler_unregister(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed);        
        ROB_LOGI(umts_ip_log_prefix, "- Deinit netif");
        esp_netif_deinit();
        ROB_LOGI(umts_ip_log_prefix, "- Destroying netif at %p.", umts_ip_esp_netif);
        esp_netif_destroy(umts_ip_esp_netif);
        
        umts_ip_esp_netif = NULL;
        ROB_LOGI(umts_ip_log_prefix, "GSM IP cleaned up.");
    } else {
        ROB_LOGW(umts_ip_log_prefix, "GSM IP cleanup was told to cleanup while not initialized or already cleaned up.");
    }

}

bool umts_ip_enable_command_mode() {
    
    // Setting command mode.    
    esp_err_t err = esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_COMMAND);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "esp_modem_set_mode failed with %i", err);
        return false;
    }
    ROB_LOGI(umts_ip_log_prefix, "Command mode set");
    return true;
}


int umts_ip_enable_data_mode() {

    // Setting data mode.
    ROB_LOGI(umts_ip_log_prefix, "esp_modem_set_data_mode(umts_dce).%p ", umts_dce);
    esp_err_t err = esp_modem_set_mode(umts_dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "esp_modem_set_data_mode(umts_dce) failed with %i", err);
        umts_ip_cleanup();
        return ESP_FAIL;
    }
    #if 0
    
        char result[64]; 
    // Attach to the GPRS service
    ROB_LOGI(umts_ip_log_prefix, "AT+CGATT=1 - %p ", umts_dce);
    esp_err_t err = esp_modem_at(umts_dce, "AT+CGATT=1", &result, 75000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "AT+CGATT=1 failed with %i", err);
        umts_ip_cleanup();
        return false;
    } else {
        ROB_LOGI(umts_ip_log_prefix, "Attach result: %s", result);
    }
    // Then set APN
    ROB_LOGI(umts_ip_log_prefix, "AT+CGDCONT=1,\"IPV4V6\",\"" CONFIG_ROBUSTO_UMTS_MODEM_PPP_APN "\" - %p ", umts_dce);
    err = esp_modem_at(umts_dce, "AT+CGDCONT=1,\"IPV4V6\",\"" CONFIG_ROBUSTO_UMTS_MODEM_PPP_APN "\"", &result, 20000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "AT+CGDCONT=1,\"PPIPV4V6P\",\"internet.telenor.se\" failed with %i", err);
        umts_ip_cleanup();
        return false;
    }

    // Check PDP state
    ROB_LOGI(umts_ip_log_prefix, "AT+CGACT? - %p ", umts_dce);
    err = esp_modem_at(umts_dce, "AT+CGACT?", &result, 150000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "AT+CGACT? failed with %i", err);
        umts_ip_cleanup();
        return false;
    } else {
        ROB_LOGI(umts_ip_log_prefix, "PDP State: %s", result);
    }
    
    // Activate PDP context
    ROB_LOGI(umts_ip_log_prefix, "AT+CGACT=1,1 - %p ", umts_dce);
    err = esp_modem_at(umts_dce, "AT+CGACT=1,1", &result, 150000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "AT+CGACT=1,1 failed with %i", err);
        umts_ip_cleanup();
        return false;
    } else {
        ROB_LOGI(umts_ip_log_prefix, "PDP Context activation result: %s", result);
    }

    // Activate PDP context
    ROB_LOGI(umts_ip_log_prefix, "AT+CGPADDR=1 %p ", umts_dce);
    err = esp_modem_at(umts_dce, "AT+CGPADDR=1", &result, 150000);
    if (err != ESP_OK)
    {
        ROB_LOGE(umts_ip_log_prefix, "AT+CGPADDR failed with %i", err);
        umts_ip_cleanup();
        return false;
    } else {
        ROB_LOGI(umts_ip_log_prefix, "PDP address result: %s", result);
    } 
    ROB_LOGI(umts_ip_log_prefix, "Data mode set, result %s", result);
    #endif   


    
    /* Wait for IP address */
    ROB_LOGI(umts_ip_log_prefix, "Waiting for IP address");
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
    robusto_conductor_server_ask_for_time(5000000);
    #endif
    EventBits_t uxBits;
    do
    {

        uxBits = xEventGroupWaitBits(umts_event_group, GSM_CONNECT_BIT | GSM_SHUTTING_DOWN_BIT, pdFALSE, pdFALSE, 4000 / portTICK_PERIOD_MS); 
        if ((uxBits & GSM_SHUTTING_DOWN_BIT) != 0)
        {
            ROB_LOGW(umts_ip_log_prefix, "Getting that we are shutting down, pausing indefinitely.");
            return -GSM_SHUTTING_DOWN_BIT;
        }
        else if ((uxBits & GSM_CONNECT_BIT) != 0)
        {
            ROB_LOGI(umts_ip_log_prefix, "Got an IP connection, great!");
            #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
            robusto_conductor_server_ask_for_time(5000);
            #endif
            return 1;
        }
        else
        {
            ROB_LOGI(umts_ip_log_prefix, "Timed out. Continuing waiting for IP.");
            #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
            robusto_conductor_server_ask_for_time(5000);
            #endif
        }
    } while (1);
 
}

void umts_ip_init(char *_log_prefix)
{
    
    umts_ip_log_prefix = _log_prefix;
    ROB_LOGI(umts_ip_log_prefix, "* Initiating umts_ip");

    /* Init and register system/core components */
    // Initialize the underlying TCP/IP stack  
    ESP_ERROR_CHECK(esp_netif_init());


    
    ROB_LOGI(umts_ip_log_prefix, " + Register IP and PPP event handlers.");
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    umts_ip_esp_netif = esp_netif_get_handle_from_ifkey("PPP_DEF");
    if (!umts_ip_esp_netif) {
        ROB_LOGI(umts_ip_log_prefix, " + No PPP_DEF netif created, create it");
           // Note that Component config > LWIP > Enable PPP support must be set.
        esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
        umts_ip_esp_netif = esp_netif_new(&netif_ppp_config);
        assert(umts_ip_esp_netif);
    }    
     
    ROB_LOGI(umts_ip_log_prefix, "* umts_ip_esp_netif assigned %p", umts_ip_esp_netif);


}
#endif