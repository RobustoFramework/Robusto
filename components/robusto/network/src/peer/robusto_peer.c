/**
 * @file robusto_peer.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The Robusto peer implementation
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <robusto_peer.h>

#include <string.h>

#include <robusto_logging.h>

#include <robusto_message.h>
#include <robusto_retval.h>
#include <robusto_system.h>
#include <robusto_media.h>
#include <robusto_qos.h>
#include <inttypes.h>

#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <esp_mac.h>
#endif


#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
#include "../media/i2c/i2c_peer.h"
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
#include "../media/lora/lora_peer.h"
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
#include "../media/ble/ble_spp.h"
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
#include "../media/espnow/espnow_peer.h"
#endif
#if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
#include "../media/canbus/canbus_peer.h"
#endif

#ifdef CONFIG_ROBUSTO_NETWORK_MOCK_TESTING
#include "../media/mock/mock_peer.h"
#endif



// TODO: Make a generic solution for persistent storage, the Arduino also have some.
// To store: Relations, 64-bit time, (if there )

struct robusto_peer robusto_host = {};

/* Just a random 32 bit number that is highly unlikely to occurr (CRC32 polynomial) */
#define HAS_RELATIONS_POLYNOMIAL 0x82F63B78

/**
 * @brief  This is an an array with all relations
 * It has two uses:
 * 1. Faster way to lookup the mac address than looping peers (discern where a communication is relevant)
 * 2. Stores relevant peers relations ids and their mac adresses in RTC,
 *    making it possible to reconnect after deep sleep
 * 3. We doesn't have to use the peer list when sending and receiving data
 *
 */
// TODO: Needs persistent storage for the other platforms aswell (do we want to make a macro?)
#ifdef USE_ESPIDF
RTC_NOINIT_ATTR struct relation relations[ROBUSTO_MAX_PEERS];
RTC_NOINIT_ATTR uint8_t relation_count;
RTC_NOINIT_ATTR uint32_t has_relations_indicator;
#else
struct relation relations[ROBUSTO_MAX_PEERS];
uint8_t relation_count;
uint32_t has_relations_indicator;
#endif


static char *peer_log_prefix;

robusto_peer_t *get_host_peer() {
    return &robusto_host;
}


/**
 * @brief Reset stats count on the peer level.
 *
 * @param peer
 */
void peer_stat_reset(robusto_media_t *stats)
{
    stats->send_successes = 0;
    stats->send_failures = 0;
    stats->receive_successes = 0;
    stats->receive_failures = 0;
    stats->score_count = 0;
    
    stats->last_send = r_millis();
    stats->last_peer_receive = stats->last_send;
    stats->last_receive = stats->last_send;
    stats->postpone_qos = false;
   
}


void init_supported_media_types(robusto_peer_t *peer)
{

#ifdef CONFIG_ROBUSTO_SUPPORTS_BLE
    if (peer->supported_media_types & robusto_mt_ble)
    {
        ble_peer_init_peer(peer);   
    }

#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_ESP_NOW
    if (peer->supported_media_types & robusto_mt_espnow)
    {
        ROB_LOGI(peer_log_prefix, "Initializing espnow peer at:");
        rob_log_bit_mesh(ROB_LOG_INFO, peer_log_prefix, peer->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
        espnow_peer_init_peer(peer);
        int rc = ROB_OK;
    }
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA
    if (peer->supported_media_types & robusto_mt_lora)
    {
        lora_peer_init_peer(peer);
        // TODO: We probably won't need to do something for LoRa here.
    }
#endif

#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    if (peer->supported_media_types & robusto_mt_i2c)
    {
        i2c_peer_init_peer(peer);
        // TODO: We probably won't need to do something for LoRa here.
    }
#endif
}

uint32_t calc_relation_id(uint8_t *mac_1, uint8_t *mac_2)
{
    uint8_t *tmp_crc_data = robusto_malloc(12);
    memcpy(tmp_crc_data, mac_1, ROBUSTO_MAC_ADDR_LEN);
    memcpy(tmp_crc_data + ROBUSTO_MAC_ADDR_LEN, mac_2, ROBUSTO_MAC_ADDR_LEN);
    // TODO: Why big endian? Change to little endian everywhere unless BLE have other ideas

    uint32_t relation_id = robusto_crc32(0, tmp_crc_data, ROBUSTO_MAC_ADDR_LEN * 2);
    robusto_free(tmp_crc_data);
    return relation_id;
}

void log_peer_info(char *_log_prefix, robusto_peer_t *peer)
{
    ROB_LOGI(_log_prefix, "Peer info:");
    ROB_LOGI(_log_prefix, "Name:                  %s", peer->name);
    ROB_LOGI(_log_prefix, "Base Mac address:      %02X:%02X:%02X:%02X:%02X:%02X", peer->base_mac_address[0],
             peer->base_mac_address[1], peer->base_mac_address[2], peer->base_mac_address[3], peer->base_mac_address[4], peer->base_mac_address[5]);
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    ROB_LOGI(_log_prefix, "I2C address:           %"PRIu8"", peer->i2c_address);
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    ROB_LOGI(_log_prefix, "CAN bus address:       %"PRIu8"", peer->canbus_address);
#endif
    ROB_LOGI(_log_prefix, "State:                 %u", peer->state);
    char mt_log[1000] = "";
    list_media_types(peer->supported_media_types, &mt_log);
    ROB_LOGI(_log_prefix, "Supported media types: %s", mt_log);
    
    ROB_LOGI(_log_prefix, "Relation id incoming:  %"PRIu32"", peer->relation_id_incoming);
    ROB_LOGI(_log_prefix, "Relation id outgoing:  %"PRIu32"", peer->relation_id_outgoing);
    ROB_LOGI(_log_prefix, "Protocol version:      %i", peer->protocol_version);
    #ifdef CONFIG_ROBUSTO_CONDUCTOR_CLIENT
    ROB_LOGI(_log_prefix, "Next availability:     %lu", peer->next_availability);
    #endif
    ROB_LOGI(_log_prefix, "Handle:                %i", peer->peer_handle);
}

uint8_t get_relation_count() {
    return relation_count;
}

void recover_relations() {

    int rel_idx = 0;
    robusto_peer_t * new_peer = NULL;
    while (rel_idx < relation_count)
    {
        ROB_LOGI(peer_log_prefix, "Adding stored peer, mac address %02X:%02X:%02X:%02X:%02X:%02X",
        relations[rel_idx].mac_address[0], relations[rel_idx].mac_address[1], relations[rel_idx].mac_address[2],
        relations[rel_idx].mac_address[3], relations[rel_idx].mac_address[4], relations[rel_idx].mac_address[5]);
        new_peer = robusto_add_init_new_peer(NULL, relations[rel_idx].mac_address, relations[rel_idx].supported_media_types);
        #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
            new_peer->i2c_address = relations[rel_idx].i2c_address;
        #endif
        #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
            new_peer->canbus_address = relations[rel_idx].canbus_address;
        #endif
        #ifdef CONFIG_ROBUSTO_CONDUCTOR_SERVER
        // As the conductor we are assuming that all existing relations are sleepers. 
        // TODO: This might not always be true obviously, but can be handled by the application
        new_peer->sleeper = true;
        #endif
        
        rel_idx++;
    }
}


rob_mac_address *relation_id_incoming_to_mac_address(uint32_t relation_id)
{
    int rel_idx = 0;
    while (rel_idx < relation_count)
    {
        if (relations[rel_idx].relation_id_incoming == relation_id)
        {
            return &(relations[rel_idx].mac_address);
        }
        rel_idx++;
    }
    ROB_LOGI(peer_log_prefix, "Relation %"PRIu32" not found.", relation_id);
    return NULL;
}

bool add_relation(uint8_t *mac_address, uint32_t relation_id_incoming, 
uint32_t relation_id_outgoing, uint8_t supported_media_types
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
                  , uint8_t i2c_address
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
                  , uint8_t canbus_address
#endif
)
{
    /* Is it already there? */
    int rel_idx = 0;
    while (rel_idx < relation_count)
    {
        if (memcmp(&(relations[rel_idx].mac_address), mac_address, ROBUSTO_MAC_ADDR_LEN) == 0)
        {
            return false;
        }
        rel_idx++;
    }
    if (relation_count >= ROBUSTO_MAX_PEERS)
    {

        ROB_LOGE(peer_log_prefix, "!!! Relations are added that cannot be accommodated, this is likely an attack! !!!");
        // TODO: This needs to be reported to monitoring
        // TODO: Monitoring should detect slow (but pointless) and fast adding of peers (similar RSSI:s indicate the same source, too).
        return false;
    }
    else
    {
        relations[relation_count].relation_id_incoming = relation_id_incoming;
        relations[relation_count].relation_id_outgoing = relation_id_outgoing;
        relations[relation_count].supported_media_types = supported_media_types;
        memcpy(&(relations[relation_count].mac_address), mac_address, ROBUSTO_MAC_ADDR_LEN);
#ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
        relations[relation_count].i2c_address = i2c_address;
#endif
#ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
        relations[relation_count].canbus_address = canbus_address;
#endif

        ROB_LOGI(peer_log_prefix, "Relation added at %hhu", relation_count);
        relation_count++;
        has_relations_indicator = HAS_RELATIONS_POLYNOMIAL;
        return true;
    }
}


robusto_media_t * get_media_info(robusto_peer_t * peer, e_media_type media_type) {

    // TODO: Should we also look at if they are supported by the peer..?
    #if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_espnow) {
        return &peer->espnow_info;
    }
    #endif
    #if defined(CONFIG_ROBUSTO_SUPPORTS_BLE) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_ble) {
        return &peer->ble_info;
    }
    #endif
    #if defined(CONFIG_ROBUSTO_SUPPORTS_LORA) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_lora) {
        return &peer->lora_info;
    }
    #endif    
    #if defined(CONFIG_ROBUSTO_SUPPORTS_I2C) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_i2c) {
        return &peer->i2c_info;
    }
    #endif  
    #if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_canbus) {
        return &peer->canbus_info;
    }
    #endif       
    #if defined(CONFIG_ROBUSTO_NETWORK_MOCK_TESTING) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if (media_type == robusto_mt_mock) {
        return &peer->mock_info;
    }
    #endif       

    ROB_LOGE(peer_log_prefix, "Invalid media type (%u) in get_media_info for peer %s", media_type, peer->name);
    r_delay(1000);
    return NULL;
}


/**
 * @brief Returns a connection score for the peer
 *
 * @param peer The peer to analyze
 * @param data_length The length of data to send
 * @return float The score = -100 don't use, +100 use
 */
float score_peer(robusto_peer_t *peer, e_media_type media_type, int data_length)
{

    robusto_media_t *curr_info = get_media_info(peer, media_type);

    ROB_LOGD(peer_log_prefix, "Mt: %s, peer: %s ss: %"PRIu32", rs: %"PRIu32", sf: %"PRIu32", rf: %"PRIu32" ", 
        media_type_to_str(media_type), peer->name, 
        curr_info->send_successes, curr_info->receive_successes,
        curr_info->send_failures, curr_info->receive_failures);

    // TODO: Add different demands, like "wired"? "secure", "fast", "roundtrip", or similar?
    // TODO: Obviously, the length score should go down if we are forced to slow down, with a low actual speed.
    float length_score = robusto_calc_suitability(media_type, data_length);

    // A failure fraction of 0.1 - 0. No failures - 10. Anything over 0.5 returns -100.
    float success_score = 10 - (curr_info->failure_rate * 100);
    
    if (success_score < -100)
    {
        success_score = -100;
    }

    // We do not include the other scores in the last score
    curr_info->last_score = success_score;

    int problem_score = 0;
    // If there are problems, discount them
    if (curr_info->state == media_state_problem) {
        problem_score = 25;
    } 

    float total_score = length_score + success_score - problem_score;
    if (total_score < -100)
    {
        total_score = -100;
    }

    ROB_LOGD(peer_log_prefix, " Scoring - Peer: %s, FR: %f, LSCR + SSCR - PSCR = TSCR => %f + %f - %i = %f", 
        peer->name, curr_info->failure_rate, length_score, success_score, problem_score, total_score);
    curr_info->last_score_time = r_millis();
 
    return total_score;
}

rob_ret_val_t set_suitable_media(robusto_peer_t *peer, uint16_t data_length, e_media_type exclude, e_media_type *result)
{
    // 
    float score = -49;
    float new_score = -50;

    *result = robusto_mt_none;

    #if defined(CONFIG_ROBUSTO_SUPPORTS_ESP_NOW) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if ((peer->supported_media_types & robusto_mt_espnow) && !(exclude & robusto_mt_espnow)) {
        new_score = score_peer(peer, robusto_mt_espnow, data_length);

        if (new_score > score && peer->espnow_info.state < media_state_recovering) {
            score = new_score;
            *result = robusto_mt_espnow;
        }
    }
    #endif

    #if defined(CONFIG_ROBUSTO_SUPPORTS_BLE) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if ((peer->supported_media_types & robusto_mt_ble) && !(exclude & robusto_mt_espnow)) {
        new_score = score_peer(peer, robusto_mt_ble, data_length);

        if (new_score > score && peer->ble_info.state < media_state_recovering) {
            score = new_score;
            *result = robusto_mt_ble;
        }
    }
    #endif
    #if defined(CONFIG_ROBUSTO_SUPPORTS_LORA) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if ((peer->supported_media_types & robusto_mt_lora) && !(exclude & robusto_mt_lora)) {
        new_score = score_peer(peer, robusto_mt_lora, data_length);

        if (new_score > score && peer->lora_info.state < media_state_recovering) {
            score = new_score;
            *result = robusto_mt_lora;
        }
    }
    #endif    
    #if defined(CONFIG_ROBUSTO_SUPPORTS_I2C) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if ((peer->supported_media_types & robusto_mt_i2c) && !(exclude & robusto_mt_i2c)) {
        new_score = score_peer(peer, robusto_mt_i2c, data_length);

        if (new_score > score && peer->i2c_info.state < media_state_recovering) {
            score = new_score;
            *result = robusto_mt_i2c;
        }
    }
    #endif       
    #if defined(CONFIG_ROBUSTO_SUPPORTS_CANBUS) || defined(CONFIG_ROBUSTO_NETWORK_QOS_TESTING)
    if ((peer->supported_media_types & robusto_mt_canbus) && !(exclude & robusto_mt_canbus)) {
        new_score = score_peer(peer, robusto_mt_canbus, data_length);

        if (new_score > score && peer->canbus_info.state < media_state_recovering) {
            score = new_score;
            *result = robusto_mt_canbus;
        }
    }
    #endif   
    if (score < -40)
    {
        return ROB_FAIL;
    } else {
        return ROB_OK;
    }
    
}

robusto_media_types get_host_supported_media_types() {
    return robusto_host.supported_media_types;
}

void add_host_supported_media_type (e_media_type supported_media_type) {
    robusto_host.supported_media_types |= supported_media_type;
}

bool peer_waitfor_at_least_state(robusto_peer_t * peer, e_peer_state state, uint32_t timeout) {
    uint32_t starttime = r_millis();
    while ((peer-state < state) && (r_millis() < (starttime + timeout))) {
        r_delay(10);
    }
    return r_millis() < (starttime + timeout);
}


/**
 * @brief
 *
 * @param _log_prefix
 */
void robusto_peer_init(char *_log_prefix)
{
    peer_log_prefix = _log_prefix;
    if (has_relations_indicator != HAS_RELATIONS_POLYNOMIAL) {
        ROB_LOGI(peer_log_prefix, "Seems to be a clean boot, no relations from before, %lu", has_relations_indicator);
        relation_count = 0;
    } else {
        #if !(defined(CONFIG_ROBUSTO_CONDUCTOR_SERVER) || defined(CONFIG_ROBUSTO_CONDUCTOR_CLIENT))
        ROB_LOGE(peer_log_prefix, "We have relations at init, that means that we rebooted unwillingly.");
        #endif
    }
    
    #ifdef USE_ESPIDF
        esp_read_mac(&(robusto_host.base_mac_address), ESP_MAC_WIFI_STA);
        ROB_LOGI(peer_log_prefix, "robusto_peer_init() - WIFI base STA address:");
        rob_log_bit_mesh(ROB_LOG_INFO, peer_log_prefix, &robusto_host.base_mac_address, ROBUSTO_MAC_ADDR_LEN);
    #endif

    robusto_host.protocol_version = ROBUSTO_PROTOCOL_VERSION;
    robusto_host.min_protocol_version = ROBUSTO_PROTOCOL_VERSION_MIN;
    if (strlen(CONFIG_ROBUSTO_PEER_NAME) > (CONFIG_ROBUSTO_PEER_NAME_LENGTH - 1)) {
        strncpy(&(robusto_host.name), CONFIG_ROBUSTO_PEER_NAME, CONFIG_ROBUSTO_PEER_NAME_LENGTH - 1);
        robusto_host.name[CONFIG_ROBUSTO_PEER_NAME_LENGTH - 1] = 0x00;
    } else {
        strcpy(&(robusto_host.name), CONFIG_ROBUSTO_PEER_NAME "\00");
    }
    #ifdef CONFIG_ROBUSTO_SUPPORTS_I2C
    robusto_host.i2c_address = CONFIG_I2C_ADDR;
    #endif
    #ifdef CONFIG_ROBUSTO_SUPPORTS_CANBUS
    robusto_host.canbus_address = CONFIG_ROBUSTO_CANBUS_ADDRESS;
    #endif
    robusto_host.supported_media_types = get_host_supported_media_types();  
 
}
