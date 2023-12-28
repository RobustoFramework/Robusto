# "Handling failure is cheaper than never failing"

Why would this statement be true? 

### Cheaper to build
If we can combine wired communication with wireless, for example, we can use much cheaper cabling and connectors. If we can use simple, off-the-shelf components and technologies.

### Cheaper to sustain
If failures aren’t catastrophic, we can take significantly more risks. Thus we may reduce maintenance and extend replacement cycles for cheaper components closer to, or maybe even beyond, expensive ones. If most of the functionality is in the code, the replacement component doesn’t have to be identical to the one replaced.

### Cheaper to monitor
An important part of a solution that can endure failure must be great reporting. 
It cannot work around issues silently, that would undermine the value of the approach.

### Cheaper to evolve
A more software (firmware) and configuration-defined system can more easily maintain backwards compatibility and change much more frequently. 

### Cheaper to secure
All wireless transmissions are susceptible to interception, if we also have a wired connection, we may use uncrackable one-time-pad encryption schemes. 

### Cheaper to protect

If either connection fails, the other can keep up the information flow. If the wireless communication is interfered with, not only can the wired pick up the slack, the wireless may use the wired connection to negotiate new frequencies or simply report that it is being interfered with. [^1]






# Reasoning

Even quality tech will fail eventually. Granted, when quality hardware fails, often it hasn't or couldn’t be maintained or has been damaged. And when great hardware has failed, from Voyager to JWST, brilliant engineers have often been able to work around this using software changes, made possible thanks to redundant pathways in the architecture. 

But outside aerospace applications and enterprise servers, the only truly redundant systems we normally own is the dual-circuit brake systems in cars. 
Instead, the approach has been to minimize failure by increasing quality, and now we have the expensive connectors and thick cabling and blame failures on poor maintenance and handling. 

While this might be perfectly true, what if we instead of preventing failures, make our systems better at handling failures? What if microcontrollers could be taught to use alternate communication paths? What if we instead of a system-wide failure got reports of a failed cable? But the system kept working?

So far, quality has been the only path to stability. But have the recent developments of microcontrollers and the IoT use cases changed this? Robusto aims to answer that question. 



[^1]: If something external caused both wired and wireless communication to fail at the same time, we probably just experienced a direct lightning strike or war conditions. Note that Robusto will try and re-establish communications, even after a broad failure.


