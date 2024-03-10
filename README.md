# AT-tiny85-additive-synth
A six oscillator additive synth for the AT tiny85 chip. The source is compiled using the Arduino IDE, and is in the standard format for that IDE. Check out my youtube channel for videos using the synth.

Developed on a Digispark board. Only 6 I/O pins are available.
* Pin 0 is the PWM output,
* Pin 1 is the (digital) Gate
* Pin 2 Pot for setting values
* Pin 3 Buttons for programming
* Pin 4 CV for note input
* Pin 5 Reset (left alone)

The buttons are a resistor chain allowing 12 buttons to be read. In my prototype, I used variable screw pots to "tune" the ladder. The CV input is 0->5V. The output can be filtered around 16kHz for better sound - I cant personally hear any noise, but the oscilloscope picks it up.

Using the synth
================
Assuming you have built a board, tuned the resistor chain and programmed the CPU, how do you use it? The buttons are broken down into 2 sections, functions and values. The value buttons select an oscillator, fixed value, or a sub menu. The function buttons select .... well, the function that is active. Buttons can be long pressed (> 0.5s) for an alternative function.

Buttons 1-6 are the value buttons, 7-12 are the functions

With no current function
-------------------------
Buttons 1-6 select oscillators 1-6
Long pressing 1-6 activates portamento. 1 is none, 6 is max

Function
---------
*note* Any function that requires an input value will read the pot value, or set the
fixed value if button 1-6 is pressed.

Button 7:
* Attack for the currently selected oscillator
* Long press for sustain level
* Button 1 is quickest attack or 100% sustain, button 6 is the longest attack, 0% sustain

Button 8:
* Release for the currently selected osciallator
* Long press for decay rate
* Button 1 is quickest release/decay, button 6 is the longest

Button 9:
* Envelope type
* Long press for LFO
* Env type button 1 => ADSR, 2 => delayed ADSR, 3=> repeating (AD)
* LFO button 1 => square, 2 => triangle, 3 => saw down, 4 => saw up, 5 => LFO speed, 6 => LFO delayed onset

Button 10:
* Volume for the selected osciallator
* Long press for tuning (separate tuneing for quantised and unquantised)
* Button 1 => 100% 2 => 50% (1/2), 3 => 33% (1/3) ......
* Tune with the pot

 Button 11:
 * Phase / sequencer
 * Long press for patch
 * Button 1 => 0 phase, 2 => 180 degree, 3 => start sequencer, 4 => Start/stop sequence input, 5 => rest, 6 => hold (not implemented yet)
 * Patch submenu:
 * Button 1 => LFO pitch strenght (pot), button 2 => Repeated note toggle, 3 => Repeated note (and sequencer) duty cycle, 4 => SNH pitch strength (pot), 5 => LFO rate snh strenght (pot), 6 => toggle quantisation.

 Button 12:
 * Load / save preset, or default the sound.
 * Short press, then short press a value button to load that preset
 * Long press, then short press a value button to save that preset
 * Long press, then long press a value button to default the sound

 When the sequence is running, pressing a value button skips to, and repeats that 8 notes. Button 1, from step 1 to 8, button 2 from step 9 to 16 etc.
