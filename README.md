
autoblind -- window blind automation, using an XOStick.
=========

background
----------
about 15 years ago i bought some surplus Makita drapery motors
(http://tinyurl.com/bqpafrt, opens in google groups), figured out
how to use them, and then never did anything with them.

in the meantime, i've done a lot of embedded programming,
including on AVR micros.  when OLPC decided to use some extra
circuit board space to create the XO Stick boards
(http://wiki.laptop.org/go/XO_Stick), i wanted to build something
with one of them, and i remembered my motors.  we've lost some
trees around the house lately, so the privacy afforded by the
bedroom window blinds has become more important.  it seemed like
the right time for some automation.

and now, in the morning, i can open the blinds at either end of
the room to see what it's like outside before i ever get out of
bed.

the project
-----------
the complete project gives full up/down control of a single
window blind.  (i've built two, one for each window.) the blind
can be controlled with a button mounted near the window (mine are
clipped to the curtain), or from a recycled IR remote.  there are
commands to raise and lower the blind.  three preset positions
may be configured for one-touch adjustment.  there's provision
for a hard limit switch, so that a runaway motor won't drag the
blind off the window.  there's also a serial console port with a
simple command-line interface, very useful for development and
debugging.

it's all written in C, using avr-gcc.  the processor on the XO
Stick is an ATtiny861.  it does _not_ use any of the USB
capabilities of the XO Stick, for either booting or operation.  i
have a USBTiny programmer for programming, and there's no need
for USB for this project.  it also doesn't use any of the Arduino
libraries -- it's all in raw C.

while my particular application is fairly specific to my needs, i
tried to write the main loop and the IR, button, timer, and
software-driven uart modules in a fairly generic way, for ease of
reuse.


the hardware
------------
the XO Stick is described here http://wiki.laptop.org/go/XO_Stick
and here http://cananian.livejournal.com/66129.html.  while the
XOrduino is way more powerful, it's overkill for most of my home
project needs, it's more expensive, and it's much harder to
solder together.  in addition, the XO Stick has some on-board
prototyping area which is very useful for the inevitable extra
components that come along with any project.

![ XO stick top ]( pix/xo_stick_top.jpg )

![ XO stick bottom ]( pix/xo_stick_bottom.jpg )

in addition to the XO Stick itself, i needed a limit switch that
could be triggered somehow by something attached to the cord on
the blind, an IR sensor, and an external pushbutton.  i built the
IR sensor and the button into the same little module, small
enough to be clipped to the curtain.  i reused the clip from a
badge holder that i got at some conference or other, and the
"case" for the module is the transparent plastic box that once
held an SD card.

![ IR sensor ]( pix/ir_receiver.jpg )

for the limit switch, i initially made my own magnetic sensor
from an old reed switch and some aluminum tubing.

![ Limit switch ]( pix/limit_switch.png )

i mounted that parallel to and even with the bottom of the window
sill, and triggered it with a magnet fastened to the blind cord. 
that worked, but was harder to mount that i'd like.  i think i'll
explore using a standard burglar-alarm style magnetic sensor
next.

the motors themselves include an odd combination of digital
hardware.  there's some 12V logic that includes closure-to-ground
control for motor on/off and direction control.  there's also a
5V circuit that drives a per-rotation pulse.

![ Motor: ]( pix/motor.jpg )
![ Motor internals ]( pix/motor_inside.jpg )

more detail of the motor [ here ]( pix/motor_gear_detail.jpg ), [
here ]( pix/motor_pcb1_detail.jpg ), and [ here ](
pix/motor_pcb2_detail.jpg ). 

i tapped into that 5V supply for the XO Stick power.  the
per-rotation pulse was useable directly, and i used a couple of
mosfets for the closure to ground inputs to the motor.  i may
have been able to do something simpler, but mosfets are pretty
simple, and i wasn't sure exactly what kind of logic family i was
connecting to.

the business end of the motor is a thing that pops out to mate
with the drapery mechanism when the motor is engaged -- it was a
way of leaving the curtains in "neutral" so that they could be
adjusted by hand when not using the motor.  i didn't use that
facility at all -- happily there's also a hole in the end of the
motor shaft, and i was able to stick in a tight-fitting nail
(with a couple of washers added) to act as a spool.

![ Cord spool ]( pix/spool.jpg )

schematics
----------
the [ XO Stick schematic ]( ./XO-Stick2-sch.pdf ) itself describes
most of the project.

the schematic for the IR receiver can practically be read from
the [ photo ]( pix/ir_receiver_close.jpg ), but here's a rough diagram:

 
        +---+   Panasonic PNA4602M or Sharp GP1UX511QS IR receiver
        | O |   (looking at the front)
        |   |
         TTT    pin 3 is Vcc
         123    pin 2 is GND
         |||    pin 1 is Vout
         |||
         ||+----+---------------- Vcc
         ||     |
         ||    ===  .1uf bypass cap
         ||     |
         |+-----+-----------------  ground     
         ||
         |+----+PB+---------------  push button
         |
         +------------------------  IR output


the pinouts i chose for the three connectors are

     LED end                                        cable color
     of board                                       ----------
                   6-pin, to motor
                        GND                         black
                        MOTOR_DIR       PA1         green
                        MOTOR_ON        PA0         blue
                        nc
                        MOTOR_TURN      PA2         brown
                        +5                          red

                    2-pin, to limit switch
                        LIMIT           PA3         yellow
                        GND                         black

                    4-pin, to IR sensor
                        GND                         black
                        BUTTON          PB2         yellow
                        IR              PA4         green
                        +5                          red
      USB end
      of board


that's about it.  oh -- i did need to add a pull-down to the motor on/off
input (PA0) to keep the motor from turning on while programming
the chip.  that 15K resistor goes from PA0 to ground.

