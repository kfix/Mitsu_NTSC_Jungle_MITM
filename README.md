Mitsu NTSC Jungle MITM

for my curbside Orion TV

based on fully-isolated MITMs for JVC pvms
https://immerhax.com/?p=185 -> https://github.com/skumlos/tb1226en-i2c-bridge
https://immerhax.com/?p=143 -> https://github.com/skumlos/ta1276an-i2c-bridge
https://immerhax.com/?p=558 -^

and one for a Sony (to handle the status-bit reads)
https://coredump.io/sony_rgb_mod/ -> https://github.com/coredump/27v66_i2c_intercept_promicro

Mitsu's jungles uses OFF (left-up) for setting its RGB input-mode to Analog and the micom repeatedly pullsit down to ON (Digital) so we must take over the line to prevent that,
and instead send a modified message (the 1st time, can skip it later) ourselves via our 2nd bus (as a master) to the jungle chip as our slave.


currenitly developed on a ESP32 (has dual I2C busses), but a STM32 Blue Pill should also be able to do this.