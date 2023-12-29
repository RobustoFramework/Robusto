# Robusto Network

Rather than sending hundreds of messages per second hoping that some are correct, Robusto verifies the integrity of all its communication. This makes it possible to notice and react to any change in the transmission environment. It also significantly reduces the bandwith used by the communication. 

These are the components of Robusto that pertains to being part of a network of Robusto peers:

## Message
This implemects the Robusto communication protocols:
* builds messages, puts it on the send queue from media queues. 
* parse messages from receive queue.
* fragments and reassembles messages that are to big for single transmissions

## Media
The implementations of the physical layers of the communitation.
Here, much of the functionality is platform specific, it is a bit of #ifdef-city here.



## Incoming
Here we make sure that each incoming messages is properly handled. 
Some messages don't get here, like messages that fails checks and heartbeats.

## Peer
The concept of the Peer is one of the pillars of Robusto. 
Here, all their management and handling is located. 
Also, it also making some choices of media, supported by QoS and peer-level-statistics.

## QoS
The QoS of Service component:
* analyzes the state of the network, detecting problems, interference, attacks
* sends heartbeats when idle to maintain a level of awareness of all the connections
* provide scores for each connection (peer/media-combination), helping to choose the best current media 
* activates recovery schemes when needed (not implementet)

## Encryption - Work in progress
Support for the encryption schemes that is made possible by the multiple media types of Robusto.

## The Robusto relation to the internet

All medias above here are physical point-to-point medias, and they have very basic adressing schemes, if any at all. 
In contrast, WiFi/UMTS/Ethernet networks:
* have their own models for authentication and encryption
* depend on external devices, like routers, switches and cell towers
* are typically connected to the internet

Robusto networks:
* should not be affected by LAN or "internet" problems, if possible not share issues
* relies partly on network isolation to not be susceptible to the common attack vectors, like wardriving for WiFi and DoS
* does not (yet) have peers with the computing power to implement proper security for the high speed of the internet
* thus needs to manage with less bandwidth, and little processing power and current draw

Instead, the Robusto approach to LAN/internet is service-oriented and more write than read.
It sends data from sensors and monitoring to MQTT or SignalK or surveillance pictures to Google Drive. 
If a Robusto network is to execute commands from the internet, it is be through channels itself has established.
 

