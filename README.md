[![Join the chat at https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im](https://badges.gitter.im/Join%20Chat.svg)](https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im?utm_source=badge&utm_medium=badge&utm_content=badge)
<!-- omit from toc -->
# The Robusto framework

**_"Handling failure is much cheaper than never failing"_** - [The Robusto paradigm](https://github.com/RobustoFramework/Robusto/blob/main/docs/About.md)

## Getting started
The easiest way is to:
1. Clone this repository, <br />
2. Open it in PlatformIO<br />
3. See if any of your MCU:s match the platform.ini ones (can also try using platform.ini.all)<br />
4. Run the examples. <br />

...or.. use [ESP-IDF](https://components.espressif.com/components/robusto/robusto)- or  [PlatformIO](https://registry.platformio.org/libraries/robusto/robusto)-dependency management and start with a blank project. A little harder, so please check out the [examples](https://github.com/RobustoFramework/Robusto/tree/main/examples/src). Bare bones projects will be added soon™.

For more general information, the [Robusto web site](https://robustoframework.github.io/Robusto/index.html) will hold everything together.
_(bit of a work in progress currently)_

<!-- omit from toc -->
# Table of contents

- [What is Robusto?](#what-is-robusto)
- [What is it not?](#what-is-it-not)
- [Tested boards](#tested-boards)
- [Work in progress (WIP)](#work-in-progress-wip)

## Documentation
More conceptual information:
- [About](https://github.com/RobustoFramework/Robusto/blob/main/docs/About.md) 
- [Architecture](https://github.com/RobustoFramework/Robusto/blob/main/docs/Architecture.md) 
- [Concepts](https://github.com/RobustoFramework/Robusto/blob/main/docs/Concepts.md) 

The more technical documentation on features are in their README:s:
 - [Base functionality](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/base/README.md) 
 - [Networking](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/network/README.md)
 - [Conductor](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/conductor/README.md)
 - [Flash](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/flash/README.md)
 - [Input](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/input/README.md)
 - [Server](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/server/README.md)
 - [Sensor](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/sensor/README.md)
 - [Misc](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/misc/README.md)

If you want to help out developing Robusto:
- [Development](https://github.com/RobustoFramework/Robusto/blob/main/development/README.md)
- [Templates](https://github.com/RobustoFramework/Robusto/blob/main/development/)

# What is Robusto?
Robusto is a framework that in some ways tries to rethink IoT development to produce cheap and robust networks, applications and services on microcontrollers.<br /> 

Features include:
* Communication
  * redundant communication
    * central in/out queues and independent queues per media
    * I2C, ESP-NOW, LoRa, BLE, CAN bus (TWAI) (and a UMTS/Cellular gateway)
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
  * Publisher subscriber (Pub sub)
  
  
..and other things typically only associated with "big" computer systems. But obviously without their memory footprint and power consumption.


# What is it not? 

Robusto is not designed to provide high-speed streaming communications for high definition video.

Instead, it is about reliably connecting sensors, actuators, controls, microcontrollers and similar components, and while it can transfer security footage and lower-grade live feeds, much more than that veers out its focus and is typically beyond the abilities and needs of the involved components[^4].
It is also a matter of [positioning](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/network/README.md#the-robusto-relation-to-the-internet).

Obviously that may change, but currently, if you want to do these things using cheap MCU:s please visit the [ESP32-Camera](https://github.com/espressif/esp32-camera) project, bringing tempered expectations. 


# Tested boards

|Microcontroller|Framework|Comments|
|----|----|----|
|ESP32-DevKit V4|ESP-IDF|Works|
|ESP32-SIM7000G|ESP-IDF|Works|
|TTGO T-BEAM SX1262|ESP-IDF|Works|
|LoRa32 V1 SX1278|ESP-IDF|Works|
|RaspberryPi Pico|Arduino|Works|
|STM32 F407VE|Arduino|Builds, uploads, boots, but untested|
|Arduino UNO*|Arduino|Too little RAM/Flash|
|Atmel ATtiny85*|Arduino|Way too little RAM/Flash|
|STM32 F03C8*|Arduino|Too little RAM/Flash|

\* Note that these chips may still communicate with Robusto peers via I2C using the Fletch16 checksum library.

# Work in progress (WIP)

* **Tell users about problems**<br/>
This will help them take the proper corrective action before problems escalate.[^3] 
* **Provide almost unbreakable wireless information security**<br/>
This is achieved by using Out-of-band (OOB) communication to transfer OTP encryption keys, 
which can be done either continuously when devices are permanently wired, or for mobile devices; while charging.[^1]



[^1]: Perhaps using a 4-pin charger connector (I2C + 5V).
[^3]: Sometimes that information, for example about radio interference may even help you troubleshoot other issues, like your WiFi network.
[^4]: Netflix or other high definition streaming services provide a pleasant viewing experience not only by using a lot bandwidht, but power-consuming real-time processing. 

