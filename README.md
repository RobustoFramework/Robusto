# The Robusto framework

## Getting started
That's not here, but [here](https://robustoframework.github.io/Robusto/index.html).  ;-)<br />
This is where you learn about Robusto itself.

## Vision
*A world where no tech builder has to choose between security, redundancy, maintainability and low cost.*
## Mission
*Provide a foundation for redundant and adaptive communication, monitoring, control and automation without relying on expensive components.*

## Description
Robusto is a framework for development of cheap and robust networks, applications and services on microcontrollers. Features include peer management, redundant communication, services, runlevels, pub sub, monitoring, configuration management and other things associated with "big" computer systems. Without their memory footprint and power consumption.

By combining different wired and wireless techniques with continuous analysis of the communication environment with the increasing abilities of MCU:s, Robusto is able to:<br/>
* **Make available some of the feel and functionality of servers**<br/>
By bringing forth some of the most important, but perhaps not too performance-hungry, [bells and whistles](https://github.com/RobustoFramework/pub_test/blob/main/components/robusto-misc/include/robusto_pubsub.h) of "real" server development, developing using Robusto feels a bit more like developing your average off-the-shelf server.
* **Work longer when networks degrade**<br/>
Even if wires start glitching or wireless networks are saturated or interfered with, systems can be kept functioning by changing techniques and even lowering speeds(WIP). 
* **Help applications to gracefully degrade their functionality**<br/>
Telling applications what communication speeds are available allows them to adapt their functionality to what the circumstances allow.[^2]
And if they don’t, they will receive helpful errors.
* **Use cheap, replaceable components and materials**<br/>
Fault-redundants networks with sensors and controls are made possible to construct on low-income, private budgets. 
Components are mass produced by several manufacturers and it should be easy to find replacements. Wires can be of lower or more flexible qualities and cheaper connectors may be used.
* **Enable agile hardware development**<br/>
The lower cost and cheaper hardware, in conjunction with thinner and easier-to-route cabling and simpler connector makes it easier to work more iteratively. 
* **Test much of you code on the PC**  
The ability to run at least most your code on native PC:s makes software development easier to make test driven, as you get to run the tests on a way faster and more powerful platform, without having to wait for firmware uploads and long build times (applies especially to ESP-IDF).

# Work in progress (WIP)

* **Very energy efficient networks**<br/>
The ability to coordinate the sleeping patterns of peers combination with ULP processors makes these networks able to run with extremely low average current draws (< 1 mA) and on low voltages (implemented in prototype, needs to be re-implemented). 
* **Tell users about problems**<br/>
This will help them take the proper corrective action before problems escalate.[^3] 
* **Provide almost unbreakable wireless information security**<br/>
This is achieved by using Out-of-band (OOB) communication to transfer OTP encryption keys, 
which can be done either continuously when devices are permanently wired, or for mobile devices; while charging.[^1]


# What is _not_ Robusto 

Robusto is *not* able to provide high-speed streaming communication for high definition video or other broadband usages. 

Instead, it is about reliably connecting sensors, actuators, controls, microcontrollers and similar components, and while it can transfer security footage and lower-grade live feeds, much more than that veers out its focus and is typically beyond the abilities and needs of the involved components[^4]. Not that it could not be added later.

[^1]: Perhaps using a 4-pin charger connector (I2C + 5V).
[^2]: I.e. frame rates may drop but still update, and other functionalities may be completely unaffected. Note that NMEA2000 with all its thick and expensive cabling and ability to support many devices, only transmits at 250kBits/s. It is quite possible to bridge communication to NMEA2000, should be added.
[^3]: Sometimes that information, for example about radio interference may even help you troubleshoot other issues, like your WiFi network.
[^4]: Netflix or other high definition streaming services provide a pleasant viewing experience by using a lot of power-consuming real-time processing. 

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