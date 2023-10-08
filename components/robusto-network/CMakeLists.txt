FILE(GLOB rob_sources 
"include/*.h"
"src/*.c"
"src/message/*.c" 
"src/incoming/*.*" 
"src/encryption/*.c" 
"src/media/*.*" 
"src/media/i2c/*.*" 
"src/media/espnow/*.*" 
"src/media/lora/*.*" 
"src/media/mock/*.*" 
"src/media/radio/*.*" 
"src/media/radio/XPowers/*.*" 
"src/media/wired/*.*" 
"src/media/spi/*.*" 
"src/peer/*.*" 
"src/qos/*.*" 
)

idf_component_register(
    SRCS ${rob_sources} 
    INCLUDE_DIRS . include 
    REQUIRES esp_timer robusto-base robusto-server RadioLib
        # ESP-NOW dependencies    
        esp_wifi esp_adc 
)