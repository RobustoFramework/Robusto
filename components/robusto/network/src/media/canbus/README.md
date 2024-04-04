# CAN bus
This is the implementation of the Robusto CAN bus media.

It adds an implementation that have all the Robusto bells and whistles, and automatically split messages in to the 8-byte frames of non-FD can. 

The reason for this is that even though I2C is as fast and perhaps is a bit easier to use, not needing transceivers and splitting messages into frames, the range of CAN, and as I perceive it, stability, is much better. 
And it is actually not much more difficult to use CAN bus transceivers like the SN65HVD230. And you can't say that even as a module, it is only $1, so not expensive.


## ESP32

For ESP32, the currently only supported tech, it uses the ESP-IDF TWAI library to add CAN bus support, or rather the ability to use a CAN bus network to communicate. 

Note that ESP-IDF not _quite_ support TWAI on boards with 26 Mhz XTAL like the LoRa32. 
The support in Robusto for those is experimental and I have raised an issue with Espressif  #(https://github.com/espressif/esp-idf/issues/13332)m,t th, awaiting formal support. It has thus currently only been tested using DevKit V5,  T-Beam LoRa and Lora32. 

Support for more Arduino oriented boards like the Raspberry Pico will be added later. 
