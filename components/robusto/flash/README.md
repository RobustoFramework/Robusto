# Robusto flash
This is the implementation of the Robusto flash support. 
It is not that hard to make flash work, but it can still be a bit cumbersome at time. Robusto tries to help out a little.

_CURRENTLY ONLY IMPLEMENTED FOR ESP-IDF_

## Why use flash?

There are main two reasons to use flash memory:
1. There is very little RAM available on an MCU compared to your PC.<br/>
 Flash can often hold several megabytes, and gigabytes if we talk external flash.
2. Flash memory is persistent. That means the the information in it won't disappear after reset. 

## Why not use flash?
Flash memory has two major drawbacks compared to RAM:
1. It is much slower than RAM. <br/>
Note however, that it is still faster than many transmission media, so that might not matter in many use cases.
1. It will break if you write too many times to it. 
While this is thousands of times, it is not really that hard to break it if you misuse it. 

# What is SPIFFS and VFS?

SPIFFS is the [SPI Flash File System](https://github.com/pellepl/spiffs?tab=readme-ov-file#spiffs-spi-flash-file-system). 

It is a file system for flash that among others are implemented in ESP-IDF and on the Arduino platforms. 

# Settings
_Flash configuration ->_

The settings are kept in this submenu in the Robusto configuration
## Enable the Robusto flash support
_-> Enable the robusto flash library_

This enables the flash library.
## Mount a SPIFF path
_-> Mount a SPIFF path_

If set, this makes Robusto flash mount the specified SPIFFS path in the VFS.
## Select the SPIFFS path to mount
_-> The SPIFFS path to mount_

Here you can select the path you want to mount. The default is /spiffs.
