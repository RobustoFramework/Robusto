# Concepts

Learning a lot of new and convoluted concepts is always a chore, so in this section, only non-technical concepts are covered.  
They should be enough for someone to be able to “work” with a Robusto network.  

*Technical concepts are in their respective sections.*


# Peer
Something that is able to partake in the communication of a Robusto network is called a peer. 

In the IoT (Internet-of-Things) parlance, a peer could be a “thing”. But as you could connect several actual “things” to a peer, and “peer” is an established word in networking. So “peer” it is.

# Relation
A peer that want to communicate, must first establishes a relation. 
In building a relation, to peers learn any relevant information about each other.  For example what ways they have of communicating with each other and when they are available next. 

Basically, it is like humans meeting. The technical concepts involved are even called things like “handshake”.

We use the word “relation” because it is a bit more formal than relationship. And that we don’t want the peers being too chummy with each other, actually they should not be very trusting.



# Media
A [transmission medium](https://en.wikipedia.org/wiki/Transmission_medium) is something that carries information. In Robusto, this term is used for the different technologies it can employ for this purpose. For example, I2C, LoRA, ESP-NOW are supported “Media”. 

When to use [“medium” or “media”](https://proofed.com/writing-tips/word-choice-media-vs-mediums/) is kind of complex, so Robusto just uses either when it wants to. It seems to be mostly “media”, though.


# Messages
A [message](https://en.wikipedia.org/wiki/Message) is, unsurprisingly, a unit of communication between two peers. In addition to the actual data being sent, it also has a preamble, with information of what kind of communication it is, if it has string data, binary data, or both.


# Service
Functionality that a Peer can provide to another peer.  
For example, a peer has an UMTS modem and is able to forward information to sites on the internet. 
Using that, a peer collecting some sensor data, can use that service to send MQTT message to mqtt.eclipseproject.io. 


# Applications
The things that the someone actually wants to happen.  
Like an autopilot remote, reacting to user clicking a button, sending an NMEA2000 message to turn 10 degrees to starbord to the peer with the NMEA2000 service.

# Runlevel
Runlevels are levels of functionality, a concept taken from Linux init.
In Robusto, it is the same, but the levels are different, only the first two levels is set by Robusto.

**Level 0**
The system has not been initialized.

**Level 1**
The system has been initialized, but not running.
All synchronous framework functions can be called, like logging, system. 

**Level 2**
Robusto base functionality and low level services has been started, like queues and networking. 
Asynchronous functionality can be used.

**Level 3**
High level services started. 
The system can respond to queries.

**Level 4**
The application is running in slept mode, an energy saving mode.
It wakes up occationally to do the things it needs, and then goes to sleep again.
It might be controlled by an orchestrator, to wake up in a timely mode.
In this mode, the network be available, but peers, while remembered on a relation id level, might not be presented yet.


TODO: Add a cached peer in RTC memory, and put the rest in flash.

**Level 5**
The application is running in woke mode. 
As in life, this means that the peer is fully aware aware and informed. 
Similarly, this take more effort, and therefore more energy,


 made by Robusto of different aspects of the conditions under which an application runs.  
They help applications gracefully adjust to a changing environment, and helps Robusto stay Robust.  
A level is always either `high`, `medium`, `low` or `problem` and may concern things like connectivity, memory or load. 

For example, a relation may have a poor connectivity, it is out of range for ESP-NOW or Wifi and has to resort to LoRa, its connection level will be `low`, and the application will have to adjust to the fact that it cannot send that picture right now, but a descriptive message will have to do. After a while, as the other peer moves close and the level increases to `medium`, pictures, but not moving images, may be sent as well. At some later point it went out of range completely and level become `problem`, and then all interaction will have to wait until until things work again. 


# Groups
A group, or peer group, is a group of peers that have decided that they have some relation to each other.  
It is usable for things like communicating the same message to several peers, collect information from the group, sharing information on services, or just to group peers for other purposes. 

Notably, the “Network” concept is absent from Robusto. Partly because Groups does all that’s needed but partly because there are already so many other “Networks”, like the Wi-fi network or the some carriers’ cell network. Also because “network” usually implies some specific technology or standard, like [802.11n](https://en.wikipedia.org/wiki/IEEE_802.11). Robusto is actively the opposite of that.

