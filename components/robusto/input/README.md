<!-- omit from toc -->
# Robusto Input

The purpose of this library is to provide some means to control a microcontroller. 
* Resistors - using voltage dividers and [ADC](https://en.wikipedia.org/wiki/Analog-to-digital_converter)s for buttons
* Touch devices - (Not implemented)

# Resistance input monitor

To be able to detect more buttons and switches with less wiring it is popular way to use ADCs to detect changes in voltage incurred by resistors being bypassed by buttons.

Robusto is currently ably to analyse and detects buttons from two variants:
1. A simple list of resistors that by themselves, or in combinations, represents one or more buttons. This is able to support a large amount of buttons.
2. Binary ladder. Here Robusto can automatically support all combinations of button presses, but as the range of ADC:s is limited, fewer buttons can be handled.

If one use many buttons, it is possible that you want to combined different approaches. 

To make it (much) easier for you to generate and calibrate your setup, there is a utility that can be enabled in menuconfig that can collect this information and create code for use in your project, see ADC monitor utility.

## Series of resistors

To facilitate just one wire and one GPIO to detect the most number of buttons, it is possible to use a series of resistors as the R<sub>2</sub> in a [voltage divider](https://en.wikipedia.org/wiki/Voltage_divider). To detect a button press, the buttons are connected to bypass a resistor when pressed. This causes a change in V<sub>out</sub> can then be detected by the ADC.

### How?

First, the input library is enabled in the menuconfig.

This will enable robusto_input.h, which defines the `resistor_monitor_t` type. In its `.mappings` property, an array `resistance_mapping_t` represents the involved resistors, with the first item representing the total resistance of R<sub>2</sub> where no buttons are pushed. 

There is also a possiblity to add an extra resistor (small, like 2200&#x2126;) to be able to detect a short circuit or failed wiring (set its value in the `resistor_monitor_t.R2_check_resistor` property).

After the rest of the information is added to the structure it can be added to the monitor list by `robusto_input_add_resistor_monitor()`. Robusto will then monitor the specified ADC channel for changes and report them using the callback. The callback provides a 32-bit value where its bits designate the pressed buttons.

## Binary ladder

A binary ladder is a more strict case where a series of resistors that relatively differ by at least half, like 128,64,32,16,8,4. When one of them becomes bypassed by a resistor, the resistance drops along with the voltage as with any series of resistors, but as they form a binary ladder, Robusto can detect all possible combinations of these buttons. 

Thus, the binary ladder is usable where one has to make the most of relatively few buttons or they have special meaning. For example, on a keyboard, putting Shift, Control, Alt, Alt Gr and similar buttons in a binary ladder provides an easy way to handle all those cases dynamically without having to manually store all those resistances. 

It doesn't come without drawbacks; it will not support as many buttons as a plain resisor series in the same voltage range, and will be more sensitive as the smallest resistors in the series will cause very small changes in voltage when maximizing the number of buttons, which on the ESP32, for example is about five using only stock resistors.

The Button ladder example shows how it works. 


# Problems and mitigations

When we, as in the button ladder example, try to cram as many buttons as possible into the same ADC, and also want to be both able to detect if they are pressed in combination, or if we have a short, we become quite sensitive. 

The main reason for this is that when the last button is pressed, that button is only 4700 &#x2126; which is only 2.5% of the total resistance 185 900 &#x2126;. As that result in a voltage of 2711&pm;4 mV, which is only 12 mV (0.4%) away from the 2723&pm;2 mV reference voltage, interference will be a problem.
Sure, the ADC is pretty precise with proper calibration, but obviously this can make the solution error-prone.

So how do we go about handling this?

## Resistor inaccuracy and build quality

These are the biggest problems; note how the calculated resistances in the resistance mapping differs significantly from the nominal values of the stock resistors. In some cases over 5%. 
There are two main reasons for this:
* Stock resistors only promise a precision of 5%
* The cables, buttons and solderings on the very quick'n dirty and experimental build are of very low quality

Basically this makes the solution unusable. But wait, where did I get those resistances? They are not the theoretical 47K and so on, they seem measured on the actual device?

This is because they were. That code was generated and sent to serial out by a utility called "Robusto ADC monitor and mapper", which is enabled in the Robusto menuconfig under Input. 

Using that, you can easily create a ladder config based on the *actual* readings the ADC does. Just push and hold the buttons in the right order and it makes several readings and calculate the standard deviations for use in the code that identifies the buttons.
The only thing it cannot calculate is R1 and the main voltage of the voltage divider

## Voltage stability
The ESP32, as is used in this example, has a quite stable voltage regulator, it actually outputs a stable voltage that is very near 3.3 V on the 3V3 pin. And it only stops working then the input voltage drops significantly, which causes instability anyway.

However, as other consumers may use the same output, we might ge voltage drops that may incorrectly be interpreted as button presses. 

Thus it is a good idea to add capacitors between 3v3 and ground near the ladder. One to protect from voltage drops, and another to handle smaller and faster fluctuations. 

## Thermal and drifting

This does not protect from slowly shifting differences, that may happen for thermal and other reasons. This is handled by the library following changes in the reference voltage.


## Measurement inaccuracy
This one is actually handled by the resistor ladder library by doing two measurements and averaging them. It also considers the provided standard deviation.

# Recommendations
The button ladder example setup with 5 buttons, which also has an extra 2200 &#x2126; resistor on R2 (value specified in resistor_monitor.R2_check_resistor) that makes it possible to detect if it has been shorted out, is probably a bit close to the limit for many cases, Especially given the limited voltage range of built-in ADCs in microcontrollers.

So just having 4 buttons per ADC would be recommended if you expect the device to work in a very [EMI](https://en.wikipedia.org/wiki/Electromagnetic_interference)-rich environment or have military grade-like requirements. Then you will have larger resistance differences making it easier to identify each button.
 
Another way would be to use a linear ladder of increasing (not doubling) resistors and maybe just handle a few combinations of linear resistances, that way it is possible to handle many more buttons.

Also, one could simply say that all buttons being pressed indicates an error state, and that way you could wiggle a little bit more margin in there.<br/>
However, this is not great either, as many buttons being pressed at once is also a normal thing that may happen and that you want to ignore or even notify (beep/vibrate) about. For example the dog sitting on the device or something.


# ADC monitor and mapper utility

This utility simplifies generation and calibration of your setup. It can be enabled in menuconfig (input->Enable the Robusto ADC monitor and mapper).

It collects voltages and configuration, and create codes for use in your project. 
An example of the output can be seen in the ladder buttons mapping configuration example. 