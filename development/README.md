# DEVELOPMENT

This folder is meant to contains the templates and tools needed for Robusto development.

## Templates

### template.h
These files should be use as templates for header files.
Note that header files for external use are put in a separate /include structure.

* A basic, doxygen doc section
    * File name, Author, version, creation date
    * A brief description<br/>(if you can't describe it briefly, maybe split it?)
    * Copyright, the 2-clause BSD license
* A `#pragma once` to ensure a [header is only included once](https://en.wikipedia.org/wiki/Pragma_once).
* Optional: C++ anti mangling <br />
If this header file is used by C++ code, you may need to have to stop the compiler from [mangling the function names](https://en.wikipedia.org/wiki/Name_mangling). 
### template.c

These files should be use as templates for source files.

* A basic, doxygen doc section
    * File name, Author, version, creation date
    * A brief description<br/>(if you can't describe it briefly, maybe split it?)
    * Copyright, the 2-clause BSD license




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
