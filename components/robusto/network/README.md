# Robusto Network

Rather than sending hundreds of messages per second hoping that some are correct, Robusto verifies the integrity of all its communication. This makes it possible to notice and react to any change in the transmission environment. It also significantly reduces the bandwith used by the communication. 

These are the components of Robusto that pertains to being part of a network of Robusto peers:

## Message
This implemects the Robusto communication protocols:
* builds messages, puts it on the send queue from media queues. 
* parse messages from receive queue.
* fragments and reassembles messages that are to big for single transmissions

### Fragmentation statistics

The fragmented-message protocol keeps cumulative in-memory statistics for large-message send and receive activity. The counters are intentionally cheap: Robusto stores one total counter set and one last-read snapshot used to calculate deltas. It does not keep rolling time buckets, so a controller, central, or diagnostics task that wants "last minute" numbers should poll the counters and calculate the interval externally.

Statistics collection is controlled at runtime with `robusto_fragment_stats_set_level()`:

* `ROBUSTO_STATS_LEVEL_OFF` disables all counter updates.
* `ROBUSTO_STATS_LEVEL_ERRORS` records protocol and resource errors, such as bad fragment references, invalid fragment indexes, wrong fragment lengths, CRC mismatches, out-of-memory events, invalid fragment types, send failures, and timeouts.
* `ROBUSTO_STATS_LEVEL_BASIC` also records message-level activity, such as fragmented sends started/succeeded, resend requests sent/received, missing fragments reported, and final result counts.
* `ROBUSTO_STATS_LEVEL_VERBOSE` also records high-frequency protocol activity, such as individual fragment messages received, checks sent/received, requests received, and fragments sent or resent.

The default level is `ROBUSTO_STATS_LEVEL_VERBOSE` when the fragmentation module is initialized. This is useful while diagnosing link or fragmentation behavior, and can be reduced later on smaller delegates if the extra counter writes are not needed.

Use `robusto_fragment_stats_get()` to read the counters:

```c
robusto_fragment_stats_t total;
robusto_fragment_stats_t delta;

robusto_fragment_stats_get(&total, &delta);
```

The `total` output receives the cumulative counters since initialization or the last `robusto_fragment_stats_reset()`. The `delta` output receives the difference since the previous call that requested a delta, and updates the internal last-read snapshot. Either output pointer may be `NULL` if only one form is needed.

Use `robusto_fragment_stats_get_level()` to inspect the current level and `robusto_fragment_stats_reset()` to clear both the cumulative counters and the delta snapshot.

When fragmentation runs on a proxy delegate, the controller can read the same counters through `robusto_proxy_client_query_fragment_stats()`. That proxy request returns both cumulative and delta counters from the delegate, so the controller or Central can log interval rates without adding rolling buckets to the delegate.

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
 

