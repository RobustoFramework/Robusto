<!-- omit from toc -->
# Robusto base
This library holds the most common cross-cutting concerns of the Robusto framework.

- [Initialization](#initialization)
- [Return values](#return-values)
- [States](#states)
- [Logging](#logging)
- [KConfig settings](#kconfig-settings)
- [Sleep functions](#sleep-functions)
- [Time functions](#time-functions)
- [GPIO handling](#gpio-handling)
- [Interrupts](#interrupts)


# Initialization
Robusto as a framework and running system, has a lot of things going. 
To make sure that everything starts in the correct order, it uses the concepts of [services](/docs/Concepts.md#service) and [runlevels](/docs/Concepts.md#runlevel). 

The `init_robusto()` function is responsible for initializing and starting all registered services in the right order, and makes the system reach a runlevel where it can return control to the caller. 

# Return values
In the `robusto_retval.h` header file, all Robusto return values are organized and defined. Note that all defined values are negative except `ROB_OK`, which is 0.

The purpose of this is to maximize the usability of return values of functions, basically if the return value is negative, an error has occurred, and you can use all other values to convey data lengths or other useful values.

# States
In `robusto_states.h` some predefined states that are used around Robusto.

# Logging
Logging is an important feature of Robusto and it tries to align to how the ESP-IDF esp_log.h functionality works and looks, but obviously on more platforms. 
Example: 
```
#include <robusto_logging.h>

char * remote_log_prefix = "ShipRemote";

// Only printed if log level is INFO
ROB_LOGI(remote_log_prefix, "Got a rudder left 10 degrees");
ROB_LOGE(remote_log_prefix, "Unhandled pub sub byte 0!");

rob_log_bit_mesh(ROB_LOG_INFO, remote_log_prefix, message->binary_data, message->binary_data_length);
```
results in:
```
I (33918) ShipRemote: Got a rudder left 10 degrees 
E (33937) ShipRemote: Unhandled pub sub byte 0!
I (33918) ShipRemote: 182(xb6) 31(x1f)  192(xc0) 214(xd6) 96(x60)  
I (33927) ShipRemote: 01101101 11111000 00000011 01101011 00000110 

```

# KConfig settings
This functionality makes Robusto projects able to use Menuconfig project settings, which is a great feature to ease configuration. 

However, as it is about supporting IDE, basically, it is _technically_ no a part of Robusto, but added as an dependency there. For example in PlatformIO, it is added using the platform.ini file. 

_It would be interesting to add this as a plugin to the Arduino development environment (or others), but that is a remaining task_

# Sleep functions
Sleeping is an essential feature of power management when it comes microcontrollers. 

It provides a way for sensors to operate on a fraction of the power consumption they would if they were to be fully powered all the time.

# Time functions
Robusto provide some functions to simplify the handling of time and timing on Microcontrollers. 

# GPIO handling
General Purpose Input/Output handling differs significantly between boards.
Robusto attempts to implement the most usual variants.

# Interrupts
Like with GPIOs, the handling is different, again, Robusto attempts to implement the most usual variants for code to be portable.