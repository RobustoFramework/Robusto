# Robusto Media
Robusto implements several medias, and is able to switch between them. 
This way, one can achieve redundancy, increase security and an ability to report health issues even if medias are down. It handles this by scoring medias based on success rate, speed, suitability and any other aspects that may be important. 

# Features
* Redundancy by using several medias
* Security by using several medias
* Re-sending
* CRC32 or Fletch16 depending on MCU (Fletch16 takes much less memory)
* Fragmentation and ressembly of large messages 

#Appendix

## Tested boards

|Microcontroller|Framework|I2C|CAN bus|LoRa|WiFi|ESP-NOW|Comments|
|----|----|----|----|----|----|---|----|
|ESP32-DevKit V4|ESP-IDF|X|X|||X|
|ESP32-SIM7000G|ESP-IDF|X|X|||X|+ works as 3G/4G [UMTS gateway](https://github.com/RobustoFramework/Robusto/tree/main/components/robusto/misc/src/umts) |
|TTGO T-BEAM SX1262|ESP-IDF|X|X|X||X||
|LoRa32 V1 SX1278|ESP-IDF|X|X|X||X||
|RaspberryPi Pico|Arduino|X|||||[I2C Peripheral times out](https://github.com/espressif/esp-idf/issues/12801)|
|STM32 F407VE|Arduino|/|||||Builds, uploads, boots, but untested|


Restricted (i.e cannot run Robusto, but can easily send data to Robusto peers using Fletch16 checksums)

|Microcontroller|Framework|I2C|
|----|----|----|
|Arduino UNO|Arduino|X|
|Atmel ATtiny85|Arduino|X|
|STM32 F03C8|Arduino|X|

