FILE(GLOB rob_sources 
"include/*.h"
"include/compat/*.h"

"base/src/init/*.c" 
"base/src/logging/*.c" 
"base/src/sleep/*.c" 
"base/src/time/*.c" 
"base/src/system/*.*" 

"conductor/src/*.c" 

"network/src/*.c"
"network/src/message/*.c" 
"network/src/incoming/*.*" 
"network/src/encryption/*.c" 
"network/src/media/*.*" 
"network/src/media/i2c/*.*" 
"network/src/media/espnow/*.*" 
"network/src/media/lora/*.*" 
"network/src/media/mock/*.*" 
"network/src/media/radio/*.*" 
"network/src/media/radio/XPowers/*.*" 
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
)

idf_component_register(
    SRCS ${rob_sources} 
    INCLUDE_DIRS . include 
    REQUIRES 
        # Base
        driver esp_timer nvs_flash 
        # Networking dependecies
        RadioLib
        # ESP-NOW dependencies    
        esp_wifi esp_adc 
        # Screen
        ssd1306
        # UMTS (note the esp_modem dependency)
        mqtt esp_netif esp_modem esp_http_client esp-tls json

)