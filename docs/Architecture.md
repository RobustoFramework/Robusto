# Architecture

The architecture of Robusto blends many different ideas and ia sometimes a library and sometimes a framework.

To understand:
* What Robusto is about and what [you are getting yourself into.](/docs/About.md).
* The [concepts of Robusto](/docs/Concepts.md).

## Design principles

### Easy to understand
If the code submitted to the framework is hard to understand, it is wrong.  
If _others_ doesn't understand _you_, _you_ are wrong[^5]. 

If you don't agree, congratulations, you almost wasted your precious time here!


### Easy to port..partially
It should be easy to implement Robusto for another platforms. 
Today, most MCU and computers comes with most of the same functionality,
can talk to each other and often both with wifi and some kind wired interface.

As they all basically have the same API:s, it should not be that hard. 
Most of the functionality of Robusto should not be platform-specific anyway.

### No more principles
Beyond that, there should be no more governing principles.<br /> 
Don't come here and point them fingers. If it works, it works.

[^5]: As a rule, Robusto will leave the most complicated stuff to 3rd party experts or libraries.