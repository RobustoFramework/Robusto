# Examples

These are examples showcasing some of the functionality of Robusto. 

Examples are run by treating the framework repository as a normal project in PlatformIO or ESP-IDF. Just build and run it using the environment settings of choice.

## Configuration

Some examples are very straight forward, and some require some configuration, like the UMTS/GSM examples. 

The configuration is made available in the menuconfig after an example has been enabled there. 

## Environments
In the cases where there is a client and server part, you till need different configurations for each MCU. 

In PlatformIO that is done using a multi environment platform.ini. See the platform.ini:s(especially the platform.ini.all) in the project for some examples of that.

In ESP-IDF, you would provide multiple sdkconfig:s:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-py.html

## Documentation

Their documentation is in their respective folders.

