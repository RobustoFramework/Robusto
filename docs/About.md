<!-- omit from toc -->
# About Robusto

What is the Robusto paradigm?
What is it really about? What is its agenda? What is its potential?


- ["Handling failure is much cheaper than never failing"](#handling-failure-is-much-cheaper-than-never-failing)
  - [Cheaper to build](#cheaper-to-build)
  - [Cheaper to sustain](#cheaper-to-sustain)
  - [Cheaper to monitor](#cheaper-to-monitor)
  - [Cheaper to evolve](#cheaper-to-evolve)
  - [Cheaper to secure](#cheaper-to-secure)
  - [Cheaper to protect](#cheaper-to-protect)
- [Reasoning](#reasoning)
  - [Nothing really works for long](#nothing-really-works-for-long)
  - [Client-side redundancy](#client-side-redundancy)
  - [Transform hardware problems into software problems](#transform-hardware-problems-into-software-problems)
- [What does it actually try to do?](#what-does-it-actually-try-to-do)



# "Handling failure is much cheaper than never failing"

The Robusto paradigm is the base assumption that the framework tries to prove.

## Cheaper to build
If we can combine wired communication with wireless, for example, we can use much cheaper cabling and connectors. We can use off-the-shelf components and controllers. Obviously, we can use expensive and specialized components, but only have to precisely where it is necessary. [^1]

## Cheaper to sustain
If failures aren’t catastrophic, we can take significantly more risks. Thus we may reduce maintenance and extend replacement cycles for cheaper components closer to, or maybe even beyond, expensive ones. If most of the functionality is in the code, the replacement component doesn’t have to be identical to the one replaced.

## Cheaper to monitor
An important part of a solution that can endure failure must be great reporting. 
It cannot work around issues silently, that would undermine the value of the approach.

## Cheaper to evolve
A more software (firmware) and configuration-defined system can more easily maintain backwards compatibility and change much more frequently. 

## Cheaper to secure
All wireless transmissions are susceptible to interception, if we also have a wired connection, we may exchange, and then use, unbreakable one-time-pad encryption schemes. 

## Cheaper to protect

If either connection fails, the other can keep up the information flow. If the wireless communication is interfered with, not only can the wired pick up the slack, the wireless may use the wired connection to negotiate new frequencies or simply report that it is being interfered with. [^2] 
It may also combine different wireless technologies, LoRa and ESP-NOW are currently implemented.


# Reasoning

For those interested in the thinking behind Robusto, and have some time to spare. :-)

## Nothing really works for long

Even quality technology will fail eventually.<br />
Granted, when quality hardware fails, often it hasn't or couldn’t be maintained or has been damaged. And when great hardware has failed, from Voyager to JWST, brilliant engineers have often been able to work around it using software changes, often made possible thanks to redundant pathways in the hardware architecture.<br />
But outside aerospace applications and enterprise servers, the only truly redundant systems we normally own is the dual-circuit brake systems in cars. 

Instead, the approach has been to minimize failure by increasing quality and sturdiness. <br />
For proof, look no further than the expensive connectors and thick cabling everywhere in your car or at the NMEA2000/SeaTalkNG network in your boat. And when those fail there is little information[^3] available. The discussion then always becomes about avoiding wear and tear, more maintenance and better handling.  

While this might be perfectly true, what if we instead of preventing failures, make our systems better at handling them? What if microcontrollers could be taught to use alternate communication paths? What if we instead of a system-wide failure, got reports of a failed cable and exactly where it is? And the system kept working? And the cable would be just something cheap and common the mechanic or oneself would even not have to order, but could easily make?

## Client-side redundancy

Assuming that quality has been the only path to stability, have the recent developments of microcontrollers and the IoT use cases changed this? Are cheap redundant architectures perhaps not that far away?

Because so far, redundancy has either been about specialized hardware, like RAID controllers, or high-powered servers. But also mostly about proxying and virtualization; systems hiding their actual architecture, providing a more stable entity by abstraction.

This is not what Robusto does. Instead, it is on the "client" side, and rather manages the complexities of choosing the currently most appropriate media for the application to reach its resource.
It is obviously on the "server" side as well, gathering incoming messages from all media and provides them to the application. But that it does on both ends, both are clients and servers. 

For example, it is quite possible that a message will use LoRa on one way, and the the response will use I2C (_receipts_ will always be in the same media though), and the application will never know.

## Transform hardware problems into software problems

Basically, this means that Robusto takes a problem that is usually handled by hardware or specialized server software and solves it locally on both ends to increase reliability. 

In general, this is a common theme in Robusto, it "softwares" itself out of hardware-related restrictions and limitations. For example, it quickly alters between being an I2C slave and peripheral to be able to  use I2C more flexibly with multiple hosts. 

# What does it actually try to do?

By combining different wired and wireless techniques with continuous analysis of the communication environment with the increasing abilities of MCU:s, Robusto is able to:<br/>
* **Make available some of the feel and functionality of "real" servers**<br/>
By bringing forth some of the most important, but perhaps not too performance-hungry, [bells and whistles](https://github.com/RobustoFramework/Robusto/blob/main/components/robusto/include/robusto_pubsub.h) of "real" server development, developing using Robusto feels a bit more like developing your average off-the-shelf server.
* **Create very energy efficient networks**<br/>
The ability to coordinate the sleeping patterns of peers make networks able to run with extremely low average current draws (< 1 mA) and on low voltages. 
* **Work longer when networks degrade**<br/>
Even if wires start glitching or wireless networks are saturated or interfered with, systems can be kept functioning by changing techniques and even lowering speeds(WIP). 
* **Help applications gracefully degrading their functionality**<br/>
Telling applications what communication speeds are available allows them to adapt their functionality to what the circumstances allow.[^4]
And if they don’t, they will receive helpful errors.
* **Use cheap, replaceable components and materials**<br/>
Fault-redundants networks with sensors and controls are made possible to construct on low-income, private budgets. 
Components are mass produced by several manufacturers and it should be easy to find replacements. Wires can be of lower or more flexible qualities and cheaper connectors may be used.
* **Enable agile hardware development**<br/>
The lower cost and cheaper hardware, in conjunction with thinner and easier-to-route cabling and simpler connector makes it easier to work more iteratively. 
* **Test much of you code on the PC**  
The ability to run at least most your code on native PC:s makes software development easier to make test driven, as you get to run the tests on a way faster and more powerful platform, without having to wait for firmware uploads and long build times (looking at you, ESP-IDF).



[^1]: Note that specialized components are not always the solution anyway. For example, vibration has been a huge issue for even hardened microprocessors when mounted directly on some VP marine diesel engines, and in those cases moving the microprocessor off the engine has been the only solution.
[^2]: If something external caused both wired and wireless communication to fail at the same time, we probably just experienced a direct lightning strike or war conditions. Note that Robusto will try and re-establish communications, even after a broad failure.
[^3]: The car stops working inexplicably and has to be brought to a mechanic, or the boat's network stops working and neither the autopilot, the wind meter or the speed log works, and the nauseating crawl to try to find the glitching cable begins. Yes, they may not only glitch, but break due to their stiffness.
[^4]: I.e. frame rates may drop but still update, and other functionalities may be completely unaffected. Note that NMEA2000 with all its thick and expensive cabling and ability to support many devices, only transmits at 250kBits/s. It is quite possible to bridge communication to NMEA2000, should be added.
[^5]: While not currently implemented like that, there are no conceptual or technical reasons beyond making the initial implementation easier to troubleshoot, but it will probably be so that a large fragmented message will be sent on many medias, and simultaneously or in an alternating. This not only for performance, but to avoid saturating an essentially shared media for too long.