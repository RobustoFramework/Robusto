FILE(GLOB rob_sources 
"include/*.h"
"include/compat/*.h"

"base/src/init/*.c" 
"base/src/logging/*.c" 
"base/src/sleep/*.c" 
"base/src/time/*.c" 
"base/src/system/*.*" 

"conductor/src/*.c" 

"flash/*.*"
"flash/include/*.h"
"flash/src/*.*"

"network/src/*.c"
"network/src/message/*.c" 
"network/src/incoming/*.*" 
"network/src/encryption/*.c" 
"network/src/media/*.*" 
"network/src/media/i2c/*.*" 
"network/src/media/ble/*.*" 
"network/src/media/canbus/*.*" 
"network/src/media/espnow/*.*" 
"network/src/media/lora/*.*" 
"network/src/media/mock/*.*" 
"network/src/media/radio/*.*" 
"network/src/media/radio/XPowers/*.*" 
"network/src/media/queue/*.*" 
"network/src/media/wired/*.*" 
"network/src/media/spi/*.*" 
"network/src/peer/*.*" 
"network/src/qos/*.*" 

"misc/src/*.c"
"misc/src/pubsub/*.c"
"misc/src/umts/*.*"

"server/src/*.c"
"server/src/queue/*.c" 
"server/src/memory/*.*" 
"server/src/concurrency/*.c" 
"server/src/repeater/*.*" 

"ui/src/*.c"

"input/src/*.c"
)

if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND rob_proxy_private_requirements
        esp_app_format
        esp_partition
        esp_system
        esp_driver_gpio
        esp_driver_sdmmc
        mbedtls
        sdmmc
        espressif__esp_serial_slave_link
    )
elseif(IDF_TARGET STREQUAL "esp32c6")
    list(APPEND rob_proxy_private_requirements
        app_update
        esp_driver_sdio
        esp_timer
        mbedtls
        nvs_flash
    )
endif()

if(CONFIG_ROBUSTO_PROXY)
    if((IDF_TARGET STREQUAL "esp32p4" OR IDF_TARGET STREQUAL "esp32c6") AND
       CONFIG_ROBUSTO_PROXY_PROFILE_STANDARD)
        message(FATAL_ERROR
            "The ESP32-P4/ESP32-C6 SDIO proxy supports only the low-memory profile")
    endif()
    list(APPEND rob_sources
        "proxy/src/robusto_proxy_control.c"
        "proxy/src/robusto_proxy_crc32.c"
        "proxy/src/robusto_proxy_frame.c"
        "proxy/src/robusto_proxy_inflight.c"
        "proxy/src/robusto_proxy_pubsub.c"
        "proxy/src/robusto_proxy_session.c"
        "proxy/transports/rsd1/src/robusto_rsd1_protocol.c"
    )
    if(IDF_TARGET STREQUAL "esp32p4")
        list(APPEND rob_sources
            "proxy/transports/esp_sdio/src/p4/robusto_proxy_sdio_host.c"
            "proxy/transports/esp_sdio/src/p4/robusto_proxy_sdio.c"
            "proxy/transports/esp_sdio/src/p4/robusto_c6_update_client.c"
            "proxy/transports/esp_sdio/src/p4/robusto_proxy_sdio_c6_provisioning.c"
        )
    elseif(IDF_TARGET STREQUAL "esp32c6")
        list(APPEND rob_sources
            "proxy/transports/esp_sdio/src/c6/robusto_proxy_sdio_device.c"
            "proxy/transports/esp_sdio/src/c6/robusto_proxy_sdio_c6.c"
            "proxy/transports/esp_sdio/src/c6/robusto_c6_proxy_server.c"
            "proxy/transports/esp_sdio/src/c6/robusto_c6_pubsub_backend.c"
            "proxy/transports/esp_sdio/src/c6/robusto_c6_control_frontend.c"
            "proxy/transports/esp_sdio/src/c6/update/robusto_c6_updater.c"
            "proxy/transports/esp_sdio/src/c6/update/robusto_c6_recovery.c"
            "proxy/transports/esp_sdio/src/c6/robusto_c6_proxy_binding.c"
        )
    endif()
    if(CONFIG_ROBUSTO_PROXY_CLIENT)
        list(APPEND rob_sources
            "proxy/src/robusto_proxy_client.c"
            "proxy/src/robusto_proxy_lifecycle.c"
            "proxy/src/robusto_proxy_pubsub_client.c"
        )
    elseif(CONFIG_ROBUSTO_PROXY_SERVER)
        list(APPEND rob_sources
            "proxy/src/robusto_proxy_pubsub_adapter.c"
            "proxy/src/robusto_proxy_service.c"
        )
    endif()
endif()

idf_component_register(
    SRCS ${rob_sources} 
    INCLUDE_DIRS . include proxy/include proxy/transports/rsd1/include
    PRIV_INCLUDE_DIRS
        proxy/transports/esp_sdio/src/common
        proxy/transports/esp_sdio/src/p4
        proxy/transports/esp_sdio/src/c6
        proxy/transports/esp_sdio/src/c6/update
    
    REQUIRES 
        # Base
        driver esp_driver_gpio esp_driver_gptimer esp_driver_i2c esp_driver_twai esp_timer nvs_flash 
        # Flash
        spiffs vfs
        # Networking dependecies
        radiolib bt
        # ESP-NOW dependencies    
        esp_wifi esp_adc 
        # Screen
        esp_lcd
        # UMTS (note the esp_modem dependency)
        esp_netif esp_modem esp_http_client esp-tls 
    PRIV_REQUIRES ${rob_proxy_private_requirements}
)