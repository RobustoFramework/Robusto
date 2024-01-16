/**
 * @file lora_messaging.cpp
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief The LoRa messaging 
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

#include "lora_messaging.hpp"
#ifdef CONFIG_ROBUSTO_SUPPORTS_LORA

#ifdef USE_ESPIDF
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_intr_alloc.h>
#include <driver/gpio.h>
#endif

#include <robusto_system.h>
#include <robusto_logging.h>
#include <robusto_time.h>
#include <robusto_message.h>
#include <robusto_concurrency.h>
#include <robusto_incoming.h>
#include <robusto_qos.h>
#include <string.h>
#include <stdio.h>

#include "lora_peer.h"
#include "lora_queue.h"
#include "../radio/RobustoHal.hpp"
#include "../spi/spi_hal.h"
#include <RadioLib.h>

/* The log prefix for all logging */
static char *lora_messaging_log_prefix;

#ifdef CONFIG_LORA_SX126X
SX1262 radio = NULL;
#endif
#ifdef CONFIG_LORA_SX127X
SX1276 radio = NULL;
#endif
RobustoHal *hal;

#if 0
#ifdef CONFIG_LORA_SX126X
#define CONFIG_LORA_SCLK_PIN 5
#define CONFIG_LORA_MISO_PIN 19
#define CONFIG_LORA_MOSI_PIN 27
#define CONFIG_LORA_CS_PIN 18
//#define CONFIG_LORA_DIO0_PIN 26
#define CONFIG_LORA_RST_PIN 23
#define CONFIG_LORA_DIO_PIN 33
#define CONFIG_LORA_BUSY_PIN 32
#endif

#ifdef CONFIG_LORA_SX127X
#define CONFIG_LORA_SCLK_PIN 5
#define CONFIG_LORA_MISO_PIN 19
#define CONFIG_LORA_MOSI_PIN 27
#define CONFIG_LORA_CS_PIN 18
#define CONFIG_LORA_DIO_PIN 26
#define CONFIG_LORA_RST_PIN 14
//#define CONFIG_LORA_DIO1_PIN 33
#endif
#endif

#define FALLING 0x3
#define RISING 0x4

// Number of times the RX flag has been triggered
volatile int RXTrigs = 0;
// flag to indicate that a packet was received
volatile bool sentFlag = false;
// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

int lora_unknown_failures = 0;
int lora_crc_failures = 0;

#ifdef USE_ESPIDF
IRAM_ATTR
#endif

int get_rssi()
{
#if 0
#ifdef CONFIG_LORA_SX126X   
    return GetRssiInst();
#endif
#ifdef CONFIG_LORA_SX127X   
    return lora_packet_rssi();
#endif
#endif
    return 0;
}

void setRXFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt)
    {
        return;
    }
    RXTrigs = RXTrigs + 1;
}

#ifdef USE_ESPIDF
IRAM_ATTR
#endif
void setTXFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt)
    {
        return;
    }
    sentFlag = true;
}

rob_ret_val_t startReceive()
{
    radio.standby();
    RXTrigs = 0;
#ifdef CONFIG_LORA_SX126X
    radio.clearDio1Action();
    radio.setDio1Action(setRXFlag);
    // TODO: This might be used for network analysis.
    int state = radio.startReceive(); // RADIOLIB_SX126X_RX_TIMEOUT_INF, RADIOLIB_SX126X_IRQ_ALL, RADIOLIB_SX126X_IRQ_ALL);
#endif
#ifdef CONFIG_LORA_SX127X
    radio.clearDio0Action();
    radio.setDio0Action(setRXFlag, RISING);
    int state = radio.startReceive();
#endif

    if (state == RADIOLIB_ERR_NONE)
    {
        ROB_LOGD(lora_messaging_log_prefix, "LoRa receiving.");
        return ROB_OK;
    }
    else
    {
        ROB_LOGE(lora_messaging_log_prefix, "LoRa failed receiving, state: %i.", state);
        return ROB_FAIL;
    }
}

rob_ret_val_t send_message(uint8_t *data, int message_len)
{

    radio.standby();
#ifdef CONFIG_LORA_SX126X
    radio.clearDio1Action();
    radio.setDio1Action(setTXFlag);
#endif
#ifdef CONFIG_LORA_SX127X
    radio.clearDio0Action();
    radio.setDio0Action(setTXFlag, RISING);
#endif
    ROB_LOGD(lora_messaging_log_prefix, ">> LoRa - will now send message using interrupt.");
    int starttime = r_millis();
    sentFlag = false;
    int16_t transmissionState = radio.startTransmit(data, message_len);
    if (transmissionState != RADIOLIB_ERR_NONE)
    {
        ROB_LOGE(lora_messaging_log_prefix, ">> LoRa failed started sending message, transmissionState: %i.", transmissionState);
        return ROB_FAIL;
    }

    int curr_wait = 0;
    while ((sentFlag == false) && (curr_wait < 6000))
    {
        curr_wait = r_millis() - starttime;
        robusto_yield();
    }
    if (curr_wait > 6000)
    {
        ROB_LOGE(lora_messaging_log_prefix, ">> LoRa timed out sending, waited 6000 ms.");
        return ROB_ERR_TIMEOUT;
    }
    else
    {
        ROB_LOGI(lora_messaging_log_prefix, ">> Message sent, sent flag set after %i ms.", curr_wait);
    }
    radio.finishTransmit();
    return ROB_OK;
}

rob_ret_val_t lora_send_message(robusto_peer_t *peer, uint8_t *data, uint32_t data_length, bool receipt)
{

    #if CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH > -1
    if (robusto_gpio_get_level(CONFIG_ROB_NETWORK_TEST_LORA_KILL_SWITCH) == true)
    {
        ROB_LOGE("LoRa", "LoRa KILL SWITCH ON - Failing receiving receipt");
        r_delay(100);
        return ROB_FAIL;
    }
    #endif
    int retval = ROB_FAIL;
    int tx_count = 0;
    int state = 0;
    uint64_t starttime;
    // We will not use all the margin in the beginning of the data
    uint8_t data_offset = 0;

    // Maximum Payload size of SX1276/77/78/79 is 255, +2 because 6 bytes is the longest addressing of LoRa, and Robusto always adds 8 bytes
    if (data_length + ROBUSTO_MAC_ADDR_LEN > 256 + 2)
    {
        ROB_LOGE(lora_messaging_log_prefix, ">> Message too long (max 250 bytes): %lu", data_length);
        // TODO: Obviously longer messages have to be possible to send.
        // Based on settings, Kbits/sec and thus time-to-send should be possible to calculate and figure out if it is too big of a message.
        retval = ROB_ERR_MESSAGE_TOO_LONG;
        goto finish;
    }

    if ((peer->state != PEER_UNKNOWN) && (peer->state != PEER_PRESENTING))
    {
        
        uint8_t relation_size = sizeof(peer->relation_id_outgoing);
        data_offset = ROBUSTO_PREFIX_BYTES - relation_size;
        // We have an established relation, use the relation id
        ROB_LOGD(lora_messaging_log_prefix, ">> Relation id > 0: %" PRIu32 ", size: %hhu.", peer->relation_id_outgoing, relation_size);

        // Add the destination address at the offset
        memcpy(data + data_offset, &(peer->relation_id_outgoing), relation_size);
    }
    else
    {
        data_offset = ROBUSTO_PREFIX_BYTES - (ROBUSTO_MAC_ADDR_LEN *2);
        ROB_LOGI(lora_messaging_log_prefix, ">> Relation id = 0, connecting using mac adresses");
        // We have no established relation, sending both mac adresses
        // Add the destination address
        memcpy(data + data_offset, &(peer->base_mac_address), ROBUSTO_MAC_ADDR_LEN);
        // Add source address
        memcpy(data + data_offset + ROBUSTO_MAC_ADDR_LEN, get_host_peer()->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
    }

    // TODO: Make some way of handling longer messages

    tx_count++;

    ROB_LOGI(lora_messaging_log_prefix, ">> Sending message: \"%.*s\", data is %lu, total %lu bytes...", (int)(data_length - 4), data + 4, data_length - data_offset, data_length);
    ROB_LOGI(lora_messaging_log_prefix, ">> Data (offset, and including addressing): ");
    rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, (uint8_t *)(data + data_offset), data_length - data_offset);

    starttime = r_millis();
    if (send_message(data + data_offset, data_length - data_offset) != ROB_OK)
    {
        ROB_LOGE(lora_messaging_log_prefix, "Failed sending..");
        retval = ROB_FAIL;
        goto finish;
    }
    peer->lora_info.last_send = r_millis();
    // We are not supposed to wait for a receipt
    if (!receipt)
    {
        retval = ROB_OK;
        goto finish;
    }
    if (startReceive() != ROB_OK)
    {
        ROB_LOGE(lora_messaging_log_prefix, "LoRa failed receiving, state: %i.", state);
        return ROB_FAIL;
    }

    // TODO: Add a check for the CRC response
    starttime = r_millis();
    do
    {

        while (RXTrigs == 0)
        {
            if (r_millis() > starttime + CONFIG_ROB_RECEIPT_TIMEOUT_MS) 
            {
                ROB_LOGE(lora_messaging_log_prefix, "<< LoRa timed out waiting for receipt from %s.", peer->name);
                r_delay(1);
                peer->lora_info.receive_failures++;
                retval = ROB_FAIL;
                goto finish;
            }
            r_delay(1);
        }
        RXTrigs = 0;
        /* TODO: Here, the data we got should be put on a receive queue.
         * Perhaps sending receipts are another "thing"? Sure they should be fast, and they should not have to wait for receiver processing.
         * Especially not when it is about chaining several messages of course, though that perhaps could be another model for that where the receiver gradually can get data.
         * Or there is no point in chaining messages if the receiver wants that..could a receiver say something about what it wants on lower levels or is this up to applications?
         */
        uint8_t buf[256]; // Maximum Payload size of SX126x/SX127x is 255/256 bytes
        int message_length = 0;

        state = radio.readData((uint8_t *)&buf, 0);
        if (state == RADIOLIB_ERR_NONE)
        {
            peer->lora_info.last_receive = r_millis();
            message_length = radio.getPacketLength(false);

            robusto_yield();

            // Relation ids are negotiated in the presenation phase
            // peer->relation_id_outgoing = calc_relation_id(peer->base_mac_address, get_host_peer()->base_mac_address);
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            // packet was received, but is malformed
            ROB_LOGW(lora_messaging_log_prefix, "<< CRC error!");
        }
        else
        {
            // some other error occurred
            ROB_LOGE(lora_messaging_log_prefix, " << LoRa Failed, code %hhu", state);
        }

        if ((memcmp(&buf, &peer->relation_id_outgoing, 4) == 0) && (message_length >= 6))
        {
            if ((buf[4] == 0xff) && buf[5] == 0x00)
            {
                ROB_LOGI(lora_messaging_log_prefix, "<< LoRa success message from %s.", peer->name);
                peer->lora_info.receive_successes++;
                peer->lora_info.send_successes++;
                peer->lora_info.last_receive = r_millis();
                retval = ROB_OK;
                goto finish;
            }
            else if (buf[4] == 0xff && buf[4] == 0x00)
            {
                ROB_LOGW(lora_messaging_log_prefix, "<< LoRa bad CRC message from %s.", peer->name);
                peer->lora_info.send_failures++;
                peer->lora_info.last_receive = r_millis();
                retval = ROB_FAIL;
                goto finish;
            }
            else
            {
                ROB_LOGE(lora_messaging_log_prefix, "<< LoRa Badly formatted CRC message from %s.", peer->name);
                rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, (uint8_t *)buf, message_length);
                peer->lora_info.receive_failures++;
                retval = ROB_FAIL;
                goto finish;
            }
        }
        else if (message_length > 0)
        {
            ROB_LOGW(lora_messaging_log_prefix, "<< LoRa got a %i-byte message to someone else (not %" PRIu32 ", will keep waiting for a response):", message_length, peer->relation_id_outgoing);
            rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, (uint8_t *)buf, message_length);
        }
        robusto_yield();
    } while (r_millis() < starttime + (CONFIG_ROB_RECEIPT_TIMEOUT_MS));
    ROB_LOGE(lora_messaging_log_prefix, "<< LoRa timed out waiting for a receipt from %s.", peer->name);

    peer->lora_info.receive_failures++;
    retval = ROB_FAIL;
    goto finish;

finish:
    if (startReceive() != ROB_OK)
    {
        ROB_LOGE(lora_messaging_log_prefix, "LoRa failed receiving, state: %i.", state);
        return ROB_FAIL;
    }
    return retval;
}

int lora_read_data(uint8_t **rcv_data_out, robusto_peer_t **peer_out, uint8_t *prefix_bytes)
{
    int retval = ROB_FAIL;
    if (RXTrigs > 0)
    {
        int currRXTrigs = RXTrigs;
        RXTrigs = 0;
        ROB_LOGD(lora_messaging_log_prefix, "<< Current RXTRigs: %i", currRXTrigs);

        uint8_t *data = (uint8_t *)robusto_malloc(255);

        ROB_LOGD(lora_messaging_log_prefix, "<< Data pointer address: %lu", (uint32_t)data);
        int message_length = 0;
    
        int state = radio.readData(data, 0);
        ROB_LOGD(lora_messaging_log_prefix, "<< After read data");
        if (state == RADIOLIB_ERR_NONE)
        {
            message_length = radio.getPacketLength(true);
            ROB_LOGD(lora_messaging_log_prefix, "<< Successfully read %i bytes of data.", message_length);
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            // packet was received, but is malformed
            message_length = radio.getPacketLength(true);
            ROB_LOGI(lora_messaging_log_prefix, "<< CRC error! Length: %u Data:", message_length);

            rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, data, message_length);
        }
        else
        {
            // some other error occurred
            ROB_LOGW(lora_messaging_log_prefix, " << Lora failed, code %hhu", state);
        }

        if (message_length > 0)
        {
            ROB_LOGI(lora_messaging_log_prefix, "<< In LoRa lora_read_data;lora_received %i bytes.", message_length);
            ROB_LOGD(lora_messaging_log_prefix, "<< Received data (including all) preamble): 0x%02X", *data);
            rob_log_bit_mesh(ROB_LOG_DEBUG, lora_messaging_log_prefix, data, message_length);
            ROB_LOGD(lora_messaging_log_prefix, "<< rob_host.base_mac_address: ");
            rob_log_bit_mesh(ROB_LOG_DEBUG, lora_messaging_log_prefix, get_host_peer()->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
        }
        // TODO: Do some kind of better non-hardcoded length check. Perhaps it just has to be longer then the mac address?
        robusto_peer_t *peer = NULL;
        rob_mac_address *src_mac_addr = NULL;
        uint8_t data_start = 0;
        if (message_length > 6)
        {
            uint32_t relation_id_outgoing = 0;

            // TODO: Add a near match on long ranges (all bits but two needs to match, emergency messaging?)
            if (memcmp(&get_host_peer()->base_mac_address, data, ROBUSTO_MAC_ADDR_LEN) == 0)
            {

                // TODO: If it is a MAC address, it is a HI-message (currently only that), those don't get receipts.
                if (message_length > (ROBUSTO_MAC_ADDR_LEN * 2))
                {
                    // We assume that the first 6 bytes are a MAC address.
                    src_mac_addr = (rob_mac_address *)(data + ROBUSTO_MAC_ADDR_LEN);
                    ROB_LOGI(lora_messaging_log_prefix, "<< src_mac_addr: ");
                    rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, get_host_peer()->base_mac_address, ROBUSTO_MAC_ADDR_LEN);
                    data_start = (ROBUSTO_MAC_ADDR_LEN * 2);
                    peer = robusto_peers_find_peer_by_base_mac_address(src_mac_addr);
                    // TODO: It is probably not correct to start calculating relation ID:s here.
                    // Basically, it is a HI-message, or it is already using an added relation id, right?
                    relation_id_outgoing = 0;
                    retval = message_length;
                }
                else
                {
                    ROB_LOGW(lora_messaging_log_prefix, "<< Matching mac but too short. Too short %d byte,:[%.*s], RSSI %i",
                             message_length, message_length, (char *)data, get_rssi());
                    retval = ROB_INFO_RECV_NO_MESSAGE;
                    robusto_free(data);
                    goto finish;
                }
            }
            else
            {
                memcpy(&relation_id_outgoing, data, 4);
                ROB_LOGD(lora_messaging_log_prefix, "Relation id in first bytes %" PRIu32 "", relation_id_outgoing);
                // TODO: Find outgoing relationid instead and perhaps a direct link to the peer.
                peer = robusto_peers_find_peer_by_relation_id_incoming(relation_id_outgoing);
                if (peer == NULL)
                {
                    ROB_LOGI(lora_messaging_log_prefix, "<< %d byte packet to someone else received:[%.*s], RSSI %f",
                             message_length, message_length, (char *)data, radio.getRSSI());
                    retval = ROB_INFO_RECV_NO_MESSAGE;
                    robusto_free(data);
                    goto finish;
                }
                data_start = sizeof(uint32_t);
            }

            ROB_LOGI(lora_messaging_log_prefix, "<< %d byte packet received:[%.*s], RSSI %f",
                     message_length, message_length, (char *)data, radio.getRSSI());

            *prefix_bytes = data_start;
            // Check the message against its CTC32 or Fletch16
            bool message_ok = robusto_check_message(data, message_length, data_start);
            // If we have an outgoing relation id, it means that the peer is identified
            if (relation_id_outgoing != 0)
            {
                // Is it a heartbeat? If so, just parse it, store stats and exit
                if ((peer) && (message_ok) && (data[data_start + ROBUSTO_CRC_LENGTH] == HEARTBEAT_CONTEXT)) {  
                    ROB_LOGD(lora_messaging_log_prefix, "<< LoRa is heartbeat");
                    peer->lora_info.last_peer_receive = parse_heartbeat(data, data_start + ROBUSTO_CRC_LENGTH + ROBUSTO_CONTEXT_BYTE_LEN);
                    add_to_history(&peer->lora_info, false, ROB_OK);
                    startReceive();
                    robusto_free(data);
                    retval = ROB_OK; 
                    goto finish;
                } 

                uint8_t response[6];
                memcpy(&response, &relation_id_outgoing, 4);

                if (message_ok)
                {
                    ROB_LOGD(lora_messaging_log_prefix, "<< Message CRC/checksum OK. Creating OK response.");
                    response[4] = 0xff;
                    response[5] = 0x00;
                    retval = message_length;
                    peer->lora_info.last_receive = r_millis();
                    peer->lora_info.receive_successes++;
                }
                else
                {
                    ROB_LOGE(lora_messaging_log_prefix, "<< Message CRC/checksum failed. Creating Fail response.");
                    response[4] = 0x00;
                    response[5] = 0xff;
                    if (peer)
                    {
                        /**
                         * @brief We only log on an existing peer, as there is a number of peers/256 risk that
                         * also the source address is wrong, as the message failed crc check.
                         * However, the likelyhood of several conflicts is very low.
                         * Also, new peers are only added if the CRC is correct, and in that case it is unlikely that errors would just
                         * concern the adress. Also, the HI message contains the mac_address.
                         */
                        peer->lora_info.receive_failures++;
                    }
                    else
                    {
                        lora_unknown_failures++;
                    }
                    retval = ROB_FAIL;
                }

                rob_log_bit_mesh(ROB_LOG_INFO, lora_messaging_log_prefix, (uint8_t *)&response, 6);

                if (send_message((uint8_t *)&response, 6) != ROB_OK)
                {
                    ROB_LOGE(lora_messaging_log_prefix, ">> LoRa failed started sending receipt.");
                    retval = ROB_FAIL;
                    robusto_free(data);
                    goto finish;
                }
            }
            else
            {
                ROB_LOGW(lora_messaging_log_prefix, "<< LoRa peer is not identified.");
                if (!message_ok)
                {
                    ROB_LOGW(lora_messaging_log_prefix, "<< LoRa peer, message failed CRC check.");
                    retval = ROB_FAIL;
                    robusto_free(data);
                    goto finish;
                }
            }
        }
        startReceive();
        if ((!peer) && (retval > 0))
        {
            peer = robusto_add_init_new_peer(NULL, src_mac_addr, robusto_mt_lora);
            peer->lora_info.last_receive = r_millis();        
            // TODO: Is it the *last* media type that should be used when responding when using the same is important?
        } 
        if (!peer) {
            ROB_LOGW(lora_messaging_log_prefix, "<< LoRa :We have no peer recognition and retval %i.", retval);
            retval = ROB_INFO_RECV_NO_MESSAGE;
            robusto_free(data);
        } else {

            // TODO: Should this thing also work without a queue?
            // uint8_t *data = *rcv_data;

            add_to_history(&peer->lora_info, false, robusto_handle_incoming(data, message_length, peer, robusto_mt_lora, data_start));
        }
        
    }
    else
    {


        retval = ROB_INFO_RECV_NO_MESSAGE;

    }

finish:

    return retval;
}

int init_radiolib()
{

#ifdef USE_ESPIDF

    spi_bus_config_t spibus_cfg = {
        .mosi_io_num = CONFIG_LORA_MOSI_GPIO,
        .miso_io_num = CONFIG_LORA_MISO_GPIO,
        .sclk_io_num = CONFIG_LORA_SCK_GPIO,
        .data3_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE,
        .flags = 0,
        .intr_flags = 0,

    };
    int state = spi_bus_initialize(SPI2_HOST, &spibus_cfg, SPI_DMA_CH_AUTO);
    if (state == ESP_OK)
    {
        ROB_LOGI(lora_messaging_log_prefix, "LoRa - spi_bus_initialize done.");
    }
    else
    {
        ROB_LOGE(lora_messaging_log_prefix, "LoRa - spi_bus_initialize failed, state: %i.", state);
        return ROB_FAIL;
    }

    spi_device_interface_config_t spi_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 128, // 50% duty cycle
        .cs_ena_pretrans = 3,
        .cs_ena_posttrans = 3,
        .clock_speed_hz = 400000,
        .input_delay_ns = 0,
        .spics_io_num = CONFIG_LORA_CS_GPIO,
        .queue_size = 3, // Keep the CS low 3 cycles after transaction, to stop slave from missing the last bit when CS has less propagation delay than CLK
        .pre_cb = NULL,
        .post_cb = NULL};

    hal = new RobustoHal(SPI2_HOST, &spi_cfg, lora_messaging_log_prefix);
#else
#error "Robust Hal is not implemented for this platform yet, only ESP-IDF"
#endif

    ROB_LOGI(lora_messaging_log_prefix, "LoRa initializing.");
#ifdef CONFIG_LORA_SX126X
    radio = new Module(hal, CONFIG_LORA_CS_GPIO, CONFIG_LORA_DIO_GPIO, CONFIG_LORA_RST_GPIO, CONFIG_LORA_BUSY_GPIO);
    int pwr = 22; // Max power for SX126X
#endif
#ifdef CONFIG_LORA_SX127X
    radio = new Module(hal, CONFIG_LORA_CS_GPIO, CONFIG_LORA_DIO_GPIO, CONFIG_LORA_RST_GPIO);
    int pwr = 20; // Max power for SX127X
#endif

    /* Common defaults */
    int cr = 5;
    float bw = 125;
    int sf = 7;

#if CONFIG_LORA_ADVANCED
    ROB_LOGI(lora_messaging_log_prefix, "Advanced LoRa settings");
    cr = CONFIG_LORA_CODING_RATE;
    bw = CONFIG_LORA_BANDWIDTH / 1000;
    sf = CONFIG_LORA_SF_RATE;
#endif

#if CONFIG_LORA_FREQUENCY_169MHZ
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is 169MHz");
    float frequency = 169; // 169MHz
#elif CONFIG_LORA_FREQUENCY_433MHZ
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is 433MHz");
    float frequency = 433; // 433MHz
#elif CONFIG_LORA_FREQUENCY_470MHZ
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is 470MHz");
    float frequency = 470; // 470MHz
#elif CONFIG_LORA_FREQUENCY_866MHZ
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is 866MHz");
    float frequency = 866; // 866MHz
#elif CONFIG_LORA_FREQUENCY_915MHZ
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is 915MHz");
    float frequency = 915; // 915MHz
#elif CONFIG_LORA_FREQUENCY_OTHER
    ROB_LOGI(lora_messaging_log_prefix, "Frequency is %d MHz", CONFIG_LORA_OTHER_FREQUENCY);
    float frequency = CONFIG_LORA_OTHER_FREQUENCY * 1000000;
#endif

#ifdef CONFIG_LORA_SX126X
    // Defaults:
    // int16_t SX1262::begin(float freq = (434.0F), float bw = (125.0F), uint8_t sf = (uint8_t)9U, uint8_t cr = (uint8_t)7U, uint8_t syncWord = (uint8_t)18U, int8_t power = (int8_t)10, uint16_t preambleLength = (uint16_t)8U, float tcxoVoltage = (1.600000024F), bool useRegulatorLDO = false)
    state = radio.begin(frequency /* freq */, bw /* bw */, sf /* sf */, cr /* cr  */, RADIOLIB_SX126X_SYNC_WORD_PRIVATE /* syncWord */, pwr /* power */); //, 16  /* preambleLength */);
#endif
#ifdef CONFIG_LORA_SX127X
    // Defaults:
    // int16_t SX1276::begin(float freq = (434.0F), float bw = (125.0F), uint8_t sf = (uint8_t)9U, uint8_t cr = (uint8_t)7U, uint8_t syncWord = (uint8_t)18U, int8_t power = (int8_t)10, uint16_t preambleLength = (uint16_t)8U, uint8_t gain = (uint8_t)0U)
    state = radio.begin(frequency /* freq */, bw /* bw */, sf /* sf */, cr /* cr  */, RADIOLIB_SX127X_SYNC_WORD /* syncWord */, pwr /* power */); //, 16  /* preambleLength */);
#endif
    // TODO: It sync word a usable concept or not?

    if (state == RADIOLIB_ERR_NONE)
    {
        ROB_LOGI(lora_messaging_log_prefix, "LoRa begun. ");
    }
    else
    {
        ROB_LOGE(lora_messaging_log_prefix, "LoRa failed starting (begin), state: %i.", state);
        return ROB_FAIL;
    }
/* Doing this after Begin, as i will fail otherwise */
/*
int crcret = radio.setCRC(0);
if (crcret != RADIOLIB_ERR_NONE) {
    ROB_LOGI(lora_messaging_log_prefix, "LoRa disabling CRC failed %i.", crcret);
}
*/
#if defined(USE_ESPIDF) && defined(CONFIG_LORA_SX127X)
    gpio_install_isr_service(0);
    ROB_LOGI(lora_messaging_log_prefix, "gpio_install_isr_service done");
#endif
    return startReceive();
}

void lora_do_on_poll_cb(queue_context_t *q_context)
{

    robusto_peer_t *peer;
    uint8_t *rcv_data;
    uint8_t prefix_bytes;

    lora_read_data(&rcv_data, &peer, &prefix_bytes);
}

void lora_do_on_work_cb(media_queue_item_t *queue_item)
{
    ROB_LOGD(lora_messaging_log_prefix, ">> In LoRa work callback.");
    send_work_item(queue_item, &(queue_item->peer->lora_info), robusto_mt_lora, &lora_send_message, &lora_do_on_poll_cb,lora_get_queue_context());
    
    lora_do_on_poll_cb(lora_get_queue_context());
}



void lora_messaging_init(char *_log_prefix)
{
    lora_messaging_log_prefix = _log_prefix;

}

#endif