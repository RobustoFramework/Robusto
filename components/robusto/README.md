# The Robusto framework

**_"Handling failure is much cheaper than never failing"_** - [The Robusto paradigm](/docs/About.md)


# What is Robusto?
Robusto is a framework that in some ways tries to rethink IoT development to produce cheap and robust networks, applications and services on microcontrollers.<br /> 

Features include:
* Communication
  * redundant communication
    * central in/out queues and independent queues per media
    * I2C, ESP-NOW, LoRa (and a little bit of UMTS/Cellular)
    * scoring media
  * peer management
    * presentation, information exchange
    * problem solving
  * retries over multple medias
  * fragmentation large messages
* Management
  * energy
    * sleeping
    * synchronized sleep patterns
  * KConfig/Menuconfig (not only for ESP-IDF, but also for Arduino, STM32)
  * monitoring / reporting / statistics
  * services
  * runlevels
* Input handling
  * Resistor array
  * Binary ladder decoder
  * ADC monitor and code generator utility
* Technical
  * flash support
  * logging
* Misc
  * UMTS/GSM gateway
  * Publisher subscriber
  
  
..and other things typically only associated with "big" computer systems. But obviously without their memory footprint and power consumption.


For more information, please visit  the [Robusto framework web site](https://robustoframework.github.io/Robusto/index.html). The [Robusto repository](https://github.com/RobustoFramework/Robusto) is where the code resides. 

_Copyright 2023_
