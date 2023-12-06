# Robusto Network

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
