# Resistor binary ladder example


This is a pretty advanced example that uses the ADC to interpret the voltage of binary resistor ladder. So you will need to know how to make that. It is also about cramming as many buttons as possible on one internal ESP32 ADC using stock resistors and being able to detect all combinations. 
*(To play it safe, see the recommendations below)*

Basically it is a series of resistors that relatively differs by at least half, like 128,64,32,16,8,4. When one of them becomes bypassed by a resistor, the resistance drops along with the voltage divider.

The example tries to use stock resistors of 100K, 47K, 22K, 10K, 4700 and finally 2200 &#x2126;. 
For R1, it uses two resistors, 33K + 8200 = 41200 &#x2126;.

So when no button is pressed, we 100K+47K+22K+10K+4700+2200 = 185 900 &#x2126;.
In the testing equipment, this results in 2723&pm;3 mV. So if button three is pressed we get 2663&pm;3 mV and thus know that this has happened.

## Problems and fixes

When we try to cram as many buttons as possible into the same ADC, and also want to be both able to detect if they are pressed in combination, or if we have a short, we become quite sensitive. 

The main reason for this is that when the last button is pressed, that button is only 4700 &#x2126; which is only 2.5% of the total resistance 185 900 &#x2126;. As that result in a voltage of 2711&pm;4 mV, which is only 12 mV (0.4%) away from the 2723&pm;2 mV reference voltage, interference will be a problem.
Sure, the ADC is pretty precise with proper calibration, but obviously this can make the solution error-prone.

So how do we go about handling this?

#### Resistor inaccuracy and build quality

These are the biggest problems; note how the calculated resistances in the resistance mapping differs significantly from the nominal values of the stock resistors. In some cases over 5%. 
There are two main reasons for this:
* Stock resistors only promise a precision of 5%
* The cables, buttons and solderings on the very quick'n dirty and experimental build are of very low quality

Basically this makes the solution unusable. But wait, where did I get those resistances? They are not the theoretical 47K and so on, they seem measured on the actual device?

This is because they were. That code was generated and sent to serial out by a utility called "Robusto ADC monitor and mapper", which is enabled in the Robusto menuconfig under Input. 

Using that, you can easily create a ladder config based on the *actual* readings the ADC does. Just push and hold the buttons in the right order and it makes several readings and calculate the standard deviations for use in the code that identifies the buttons.
The only thing it cannot calculate is R1 and the main voltage of the voltage divider

#### Voltage stability
The ESP32, as is used in this example, has a quite stable voltage regulator, it actually outputs a stable voltage that is very near 3.3 V on the 3V3 pin. And it only stops working then the input voltage drops significantly, which causes instability anyway.

However, as other consumers may use the same output, we might ge voltage drops that may incorrectly be interpreted as button presses. 

Thus it is a good idea to add capacitors between 3v3 and ground near the ladder. One to protect from voltage drops, and another to handle smaller and faster fluctuations. 

#### Thermal and drifting

This does not protect from slowly shifting differences, that may happen for thermal and other reasons. This is handled by the library following changes in the reference voltage.


#### Measurement inaccuracy
This one is actually handled by the resistor ladder library by doing two measurements and averaging them. It also considers the provided standard deviation.

### Recommendations
The example setup with 5 buttons, which also has an extra 2200 &#x2126; level that makes it possible to detect if it has been shorted out, is probably going a bit close to the limit. 

So just having 4 buttons per ADC would be recommended if you expect the device to work in a very [EMI](https://en.wikipedia.org/wiki/Electromagnetic_interference)-rich environment or have military grade-like requirements.
 
Another way would be to use a linear ladder of increasing (not doubling) resistors and  maybe just handle a few combinations of linear resistances, that way it is possible to handle many more buttons.

Also, one could simply say that all buttons being pressed indicates an error state, and that way you could wiggle a little bit more margin in there.<br/>
However, this is not great either, as many buttons being pressed at once is also a normal thing that may happen and that you want to ignore or even notify (beep/vibrate) about. For example the dog sitting on the device or something.

