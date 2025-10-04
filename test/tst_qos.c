
#include "tst_qos.h"
// TODO: Add copyrights on all these files
#include <unity.h>


#include <stdbool.h>
#include <string.h>

#include <robusto_logging.h>
#include <robusto_message.h>
#include "tst_defs.h"
#include <robusto_peer_def.h>
#include <robusto_media.h>
#include <robusto_qos.h>

static robusto_peer_t *remote_peer = NULL;

void qos_init_defs() {
	if(remote_peer != NULL) {
		return;
	}
	remote_peer = robusto_add_init_new_peer("TEST_QOS", (rob_mac_address *)kconfig_mac_to_6_bytes(*(uint64_t *)&ROBUSTO_MAC_ADDRESS_BASE), robusto_mt_lora);

	remote_peer->protocol_version = 0;
	remote_peer->relation_id_outgoing =	TST_RELATIONID_01;
	remote_peer->peer_handle = 0;
	remote_peer->state = PEER_KNOWN_INSECURE; 
	// We use macs in all initial communication. 

	
}

/**
 * @brief Tests the scoring mechanism of the media without doing any actual sending.
 * @note Therefore, it can be run in the Native environment.
 */
void tst_qos(void)
{
    qos_init_defs();

    /* Init and check that initial state is working */
    robusto_media_types orig_support = remote_peer->supported_media_types;
    // Enable all media types
    remote_peer->supported_media_types = robusto_mt_espnow | robusto_mt_i2c | robusto_mt_lora;
    
    e_media_type media_type;
    rob_ret_val_t test_res_0 = set_suitable_media(remote_peer, 100, 100, &media_type);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(test_res_0, ROB_OK, "0. set_suitable_media failed.");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(robusto_mt_espnow, media_type, "0. Wrong default suitable media type");
    
    #if 0
    // char data[9] = "ABCDEFGH\0";
     char data[6] = "\x00\x0F\xF0\x0F\xAA\x00";
    rob_log_bit_mesh(ROB_LOG_INFO, "Test_Tag", &data, sizeof(data) - 1);
    strcpy(data, "\xFF\xF0\xBC\xBC\xBC\x00");
    #endif
    robusto_media_t * peer_info_1 = get_media_info(remote_peer, robusto_mt_espnow);
    add_to_failure_rate_history(peer_info_1, 1);
    add_to_failure_rate_history(peer_info_1, 1);
    add_to_failure_rate_history(peer_info_1, 1);
    add_to_failure_rate_history(peer_info_1, 1);
    e_media_type second_media;
    rob_ret_val_t test_res_1 = set_suitable_media(remote_peer, 100, 100, &second_media);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(ROB_OK, test_res_1, "1. set_suitable_media failed.");
    
    ROB_LOGI("TEST", "Media selected 1: %s", media_type_to_str(second_media));
    TEST_ASSERT_NOT_EQUAL_INT16_MESSAGE(second_media, robusto_mt_espnow, "1. Suitable media haven't changed.");
    
    

    robusto_media_t * peer_info_2 = get_media_info(remote_peer, second_media);
    add_to_failure_rate_history(peer_info_2, 1);
    add_to_failure_rate_history(peer_info_2, 1);
    add_to_failure_rate_history(peer_info_2, 1);
    add_to_failure_rate_history(peer_info_2, 1);
    add_to_failure_rate_history(peer_info_2, 1);

    rob_ret_val_t test_res_2 = set_suitable_media(remote_peer, 100, 100, &media_type);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(ROB_OK, test_res_2, "2. set_suitable_media failed.");
    ROB_LOGI("TEST", "Media selected 2: %s", media_type_to_str(media_type));
    TEST_ASSERT_NOT_EQUAL_INT16_MESSAGE(media_type, robusto_mt_espnow, "2. Suitable media is ESP-NOW (first media).");
    TEST_ASSERT_NOT_EQUAL_INT16_MESSAGE(media_type, second_media, "2. Suitable media did't change.");
    
    remote_peer->supported_media_types = orig_support;

}
