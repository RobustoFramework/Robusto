[![Join the chat at https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im](https://badges.gitter.im/Join%20Chat.svg)](https://app.gitter.im/#/room/!WtMzOtrXhykXImnhsr:gitter.im?utm_source=badge&utm_medium=badge&utm_content=badge)
# The Robusto framework

## Getting started
The easiest way is to:
1. Clone this repository, <br />
2. Open it in PlatformIO<br />
3. See if any of your MCU:s match the platform.ini ones (can also try using platform.ini.all)<br />
4. Run the examples. <br />

For more information, the [Robusto web site](https://robustoframework.github.io/Robusto/index.html) holds everything togetether.

## What is Robusto?
Robusto is a framework for development of cheap and robust networks, applications and services on microcontrollers.<br /> 

Features include:
* Communication
  * peer management
    * presentation, information exchange
    * problem solving
  * redundant communication
    * indepentent queues per media
    * I2C, ESP-NOW, LoRa
    * scoring media
  * retryings
  * fragmentation large message
* Management
  * energy
    * sleeping
    * synchronized sleep patterns
  * runlevels
  * configuration (KConfig not only for ESP-IDF, but also for Arduino, STM32)
  * monitoring / reporting
  * services
* Technical
  * flash support
  * logging
* Misc
  * UMTS/GSM gateway
  * Publisher subscriber
  
  
..and other things associated with "big" computer systems. But obviously without their memory footprint and power consumption.

By combining different wired and wireless techniques with continuous analysis of the communication environment with the increasing abilities of MCU:s, Robusto is able to:<br/>
* **Make available some of the feel and functionality of "real" servers**<br/>
By bringing forth some of the most important, but perhaps not too performance-hungry, [bells and whistles](https://github.com/RobustoFramework/pub_test/blob/main/components/robusto-misc/include/robusto_pubsub.h) of "real" server development, developing using Robusto feels a bit more like developing your average off-the-shelf server.
* **Create very energy efficient networks**<br/>
The ability to coordinate the sleeping patterns of peers make networks able to run with extremely low average current draws (< 1 mA) and on low voltages. 
* **Work longer when networks degrade**<br/>
Even if wires start glitching or wireless networks are saturated or interfered with, systems can be kept functioning by changing techniques and even lowering speeds(WIP). 
* **Help applications gracefully degrading their functionality**<br/>
Telling applications what communication speeds are available allows them to adapt their functionality to what the circumstances allow.[^2]
And if they don’t, they will receive helpful errors.
* **Use cheap, replaceable components and materials**<br/>
Fault-redundants networks with sensors and controls are made possible to construct on low-income, private budgets. 
Components are mass produced by several manufacturers and it should be easy to find replacements. Wires can be of lower or more flexible qualities and cheaper connectors may be used.
* **Enable agile hardware development**<br/>
The lower cost and cheaper hardware, in conjunction with thinner and easier-to-route cabling and simpler connector makes it easier to work more iteratively. 
* **Test much of you code on the PC**  
The ability to run at least most your code on native PC:s makes software development easier to make test driven, as you get to run the tests on a way faster and more powerful platform, without having to wait for firmware uploads and long build times (looking at you, ESP-IDF).

# What is it not? 

Robusto is *not* able to provide high-speed streaming communication for high definition video or other broadband usages. 

Instead, it is about reliably connecting sensors, actuators, controls, microcontrollers and similar components, and while it can transfer security footage and lower-grade live feeds, much more than that veers out its focus and is typically beyond the abilities and needs of the involved components[^4]. Not that it could not be added later.

[^1]: Perhaps using a 4-pin charger connector (I2C + 5V).
[^2]: I.e. frame rates may drop but still update, and other functionalities may be completely unaffected. Note that NMEA2000 with all its thick and expensive cabling and ability to support many devices, only transmits at 250kBits/s. It is quite possible to bridge communication to NMEA2000, should be added.
[^3]: Sometimes that information, for example about radio interference may even help you troubleshoot other issues, like your WiFi network.
[^4]: Netflix or other high definition streaming services provide a pleasant viewing experience by using a lot of power-consuming real-time processing. 

# Table of contents

 - [Base](/components/robusto/base/README.md)
 - [Networking](/components/robusto/networking/README.md)
 - [Conductor](/components/robusto/conductor/README.md)
 - [Flash](/components/robusto/flash/README.md)
 - [Server](/components/robusto/server/README.md)
 - [Sensor](/components/robusto/sensor/README.md)
 - [Misc](/components/robusto/misc/README.md)


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



[^5]: As a rule, Robusto will leave the most complicated stuff to 3rd party experts or libraries.
