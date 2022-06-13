# Mitsu NTSC Jungle MITM

based on fully-isolated MITMs for JVC PVMs:
https://immerhax.com/?p=185 -> https://github.com/skumlos/tb1226en-i2c-bridge
https://immerhax.com/?p=143 -> https://github.com/skumlos/ta1276an-i2c-bridge
https://immerhax.com/?p=558 -^

and one for a Sony (to handle the status-bit reads)
https://coredump.io/sony_rgb_mod/ -> https://github.com/coredump/27v66_i2c_intercept_promicro

Mitsu's jungles uses OFF (left-up) for setting its RGB input-mode to Analog and the micom repeatedly pullsit down to ON (Digital) so we must take over the line to prevent that,
and instead send a modified message (the 1st time, can skip it later) ourselves via our 2nd bus (as a master) to the jungle chip as our slave.

# project status

This is being used to run a rebadged Orion 20" TV as a RGB monitor
for retro-gaming.

Currently devleloped against a Broksonic CTGV-5463TT:
* "MICOM" is an Orion OEC7044A
* "Jungle chip" is a M61203BFP NTSC processor

I'm using an ESP32 devboard (EzSBC ESP32U-01), but a STM32 Blue/Black Pill _should_ also be able to do this.  
Both have 5V tolerant digital-IOs (which this TV uses for all logic chips) and have dual I2C bus support, so
we don't need to use SoftI2CMaster and the logic can be fully interrupt driven.

My modded TV is handling native RGB output from a Genesis using an attenuated cable with a clean CSync.  
I've also used it with Raspberry Pi 3B+ & 4 with the VGA666 boards running Recalbox OS 8.1.  
They needed their Hsync (13) & Vsync (14) pins bridged together at the HD15 jack.

# howto RGB Mod

Go find The SegaHolic on youtube for lots of good videos on how to teardown  
& analyze your candidate TV to install external R/G/B, composite-sync, and blanking inputs.  
If this is your 1st one, be prepared for it to become broken or find another one to practice on.  

Mine didn't even have a composite-video jack, so I started with just that to get a feel for it.  
This line later was used to inject the sync signal for the full RGB-mod.   

Your initial goal is to just substitute the OSD graphics with your test console's RGB output.  

You don't need to spend time on a "OSD Mux" resistor-divider for these inputs quite yet,
just use a wide enough breadboard for those extra resistors to be added between the input harness and
the tapped lines.

Make really sure your input cable is attenuated for low-voltage 0.7Vpp 75ohm input on all lines!  
The cheap cables are crapshoots so check them with a multimeter for resistance with ground.
If it has a SCART head, you should crack it open.

If you're lucky and got the signal displayed, then you don't need this MCU project at all, cool!    
For bonus points, figure out the right Muxing resistors for the onboard OSD ("OSD Mux RGB Mod") so you 
can keep its lines connected even while your RGB-blanking-switch is on.

But if the swapped picture looks "binary", then your MICOM might be like mine and is setting
the Jungle to treat OSD as "Digital RGB". We're gonna need a bigger breadboard....

# howto I2C MITM

If you think the screen is worth this much trouble, then go one step further
and plumb 4 more taps for I2C data & clock lines:
* 2 between the Chroma chip & ESP32
* 2 between Jungle chip & ESP32.

My TV had a 100 Ohm SMD resistor on both lanes just off of the Jungle chip,  just like in its
datasheet's application example. There were easy to replace with IN/OUT taps to the breadboard.  
I used 4 new 100 Ohm inline resistors between the ESP32 and these taps. I also mapped the Jungle's
lines to the specific SDA0/SCL0 pins that have internal pullups - these seem sufficient to condition
the bus. When I tried using external pullups to my TV's 5v rail, I got failures to boot.

I also had to source power from an external supply, my TV's 5V rail for the micom/jungle sagged when the
ESP32 was connected, resetting it and probably the other chips.  
an AC120V->3.3V 800ma module was cheap & easy to shoehorn onto the motherboard after I desoldered
the audio jack for the crappy internal speaker. I also cut the power pins to the (mono) sound amp.  