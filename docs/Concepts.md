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


# Message
A [message](https://en.wikipedia.org/wiki/Message) is, unsurprisingly, a unit of communication between two peers. In addition to the actual data being sent, it also has a preamble, with information of what kind of communication it is, if it has string data, binary data, or both.


# Service 
For example, a peer has an UMTS modem and is able to forward information to sites on the internet. Using that, a function collecting some sensor data, can use that service to send MQTT message to mqtt.eclipseproject.io. 
Services are started and stopped depending on what runlevel (see below) the want to belong to.

# Network service
A network service is functionality that a Peer can provide to _another peers_, basically it is a callback to which requests are routed if they provide the proper service id.
An example would be a service on a boat that collects speed and sensor data from legacy functions and sends it to an NMEA 2000 network. 

## Combinations

It is quite possible, and quite normal, to combine services and network services.
One example is the Publisher Subscriber service in Robusto-misc, whose queue is started using a service. Code running on the same peer can interact with the service. But a network service is also started to respond to publications and subscription request.

# Applications
The things that the someone actually wants to happen.  
Like an autopilot remote, reacting to user clicking a button, sending an NMEA2000 message to turn 10 degrees to starbord to the peer with the NMEA2000 service.

# Runlevel
Runlevels are levels of functionality, a concept taken from (Linux init)[https://en.wikipedia.org/wiki/Runlevel]. In Robusto, it is the same, but the levels are different, only the first two levels is set by Robusto.

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
The system can respond to requests.

**Level 4**
The application is running in slept mode, an energy saving mode.
It wakes up occationally to do the things it needs, and then goes to sleep again.
It might be controlled by an Conductor, to wake up in a timely mode.
In this mode, the network be available, but peers, while remembered on a relation id level, might not be presented yet.

**Level 5**
The application is running in woke mode. 
As in life, this means that the peer is fully aware aware and informed. 
Similarly, this take more effort, and therefore more energy,


# Work in progress
The following concepts are WIP and
## Connection level
The connection level is  made by Robusto of different aspects of the conditions under which an application runs.  
They help applications gracefully adjust to a changing environment, and helps Robusto stay Robust.  
A level is always either `high`, `medium`, `low` or `problem` and may concern things like connectivity, memory or load. 

For example, a relation may have a poor connectivity, it is out of range for ESP-NOW or Wifi and has to resort to LoRa, its connection level will be `low`, and the application will have to adjust to the fact that it cannot send that picture right now, but a descriptive message will have to do. After a while, as the other peer moves close and the level increases to `medium`, pictures, but not moving images, may be sent as well. At some later point it went out of range completely and level become `problem`, and then all interaction will have to wait until until things work again. 


## Groups
A group, or peer group, is a group of peers that have decided that they have some relation to each other.  
It is usable for things like communicating the same message to several peers, collect information from the group, sharing information on services, or just to group peers for other purposes. 



