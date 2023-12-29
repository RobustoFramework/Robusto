[![Join the chat at https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im](https://badges.gitter.im/Join%20Chat.svg)](https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im?utm_source=badge&utm_medium=badge&utm_content=badge)
<!-- omit from toc -->
# The Robusto framework

**_"Handling failure is cheaper than never failing"_** - [The Robusto paradigm](paradigm.md)

## Getting started
The easiest way is to:
1. Clone this repository, <br />
2. Open it in PlatformIO<br />
3. See if any of your MCU:s match the platform.ini ones (can also try using platform.ini.all)<br />
4. Run the examples. <br />

For more general information, the [Robusto web site](https://robustoframework.github.io/Robusto/index.html) will hold everything together.
_(bit of a work in progress currently)_

<!-- omit from toc -->
# Table of contents

- [Documentation](#documentation)
- [What is Robusto?](#what-is-robusto)
- [What is it not?](#what-is-it-not)
- [Tested boards](#tested-boards)
- [Work in progress (WIP)](#work-in-progress-wip)
- [Architecture](#architecture)
  - [Design principles](#design-principles)
    - [Easy to understand](#easy-to-understand)
    - [Easy to port..partially](#easy-to-portpartially)
    - [No more principles](#no-more-principles)

# Documentation
The more technical documentation is in the README:s in this repository:
   - [Base functionality](/components/robusto/base/README.md) 
   - [Networking](/components/robusto/network/README.md)
   - [Conductor](/components/robusto/conductor/README.md)
   - [Flash](/components/robusto/flash/README.md)
   - [Server](/components/robusto/server/README.md)
   - [Sensor](/components/robusto/sensor/README.md)
   - [Misc](/components/robusto/misc/README.md)


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
* Technical
  * flash support
  * logging
* Misc
  * UMTS/GSM gateway
  * Publisher subscriber
  
  
..and other things typically only associated with "big" computer systems. But obviously without their memory footprint and power consumption.


# What is it not? 

Robusto is *not* able to provide high-speed streaming communication for high definition video or other broadband usages. 

Instead, it is about reliably connecting sensors, actuators, controls, microcontrollers and similar components, and while it can transfer security footage and lower-grade live feeds, much more than that veers out its focus and is typically beyond the abilities and needs of the involved components[^4]. Not that it could not be added later.


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

\* Note that these chips may till communicate with Robusto Peers via I2C using the Fletch16 checksum library.

# Work in progress (WIP)

* **Tell users about problems**<br/>
This will help them take the proper corrective action before problems escalate.[^3] 
* **Provide almost unbreakable wireless information security**<br/>
This is achieved by using Out-of-band (OOB) communication to transfer OTP encryption keys, 
which can be done either continuously when devices are permanently wired, or for mobile devices; while charging.[^1]

# Architecture

The architecture of Robusto blends many different ideas and sometimes be a library and sometimes a framework.
## Design principles

### Easy to understand
If the code submitted to the framework is hard to understand, it is wrong.  
If others doesn't understand you, you are wrong[^5]. 

If you don't agree, congratulations, you almost wasted your precious time here!


### Easy to port..partially
It should be easy to implement Robusto for another platforms. 
Today, most MCU and computers comes with most of the same functionality,
can talk to each other and often both with wifi and some kind wired interface.
As they all basically have the same API:s, it should not be that hard. 
Most of the functionality of Robusto should not be platform-specific anyway.

### No more principles
Beyond that, there should be no more governing principles. 
Don't come here and point them fingers. If it works, it works.


[^1]: Perhaps using a 4-pin charger connector (I2C + 5V).
[^3]: Sometimes that information, for example about radio interference may even help you troubleshoot other issues, like your WiFi network.
[^4]: Netflix or other high definition streaming services provide a pleasant viewing experience by using a lot of power-consuming real-time processing. 
[^5]: As a rule, Robusto will leave the most complicated stuff to 3rd party experts or libraries.
