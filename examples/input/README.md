# Resistor binary ladder example

This is a pretty advanced example that uses the ADC to interpret the voltage of binary resistor ladder. So you will need to know how to make that. It is also about cramming as many buttons as possible on one internal ESP32 ADC using stock resistors and being able to detect all combinations. 
*(To play it safe, see the recommendations below)*

Basically it is a series of resistors that relatively differs by at least half, like 128,64,32,16,8,4. When one of them becomes bypassed by a resistor, the resistance drops along with the voltage divider.

The example tries to use stock resistors of 100K, 47K, 22K, 10K, 4700 and finally 2200 &#x2126;. 
For R1, it uses two resistors, 33K + 8200 = 41200 &#x2126;.

So when no button is pressed, we 100K+47K+22K+10K+4700+2200 = 185 900 &#x2126;.
In the testing equipment, this results in 2723&pm;3 mV. So if button three is pressed we get 2663&pm;3 mV and thus know that this has happened.
