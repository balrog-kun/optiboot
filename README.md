Optiboot for nRF24L01+
======================

This is a modifed Optiboot bootloader for Arduino.  The two main features I added are:

*   support for the nRF24L01+ radio transciever chip for wireless reprogramming of
    your Arduinos.
*   implemented SUPPORT_EEPROM so eeprom flashing now actually works.

The nRF24L01+ is a cheap SPI radio transciever chip that works in simplex mode (can't receive
and transmit at the same time, unlike serial communication).  This version of optiboot can
receive commands from a program like avrdude or an Arduino IDE either through serial or
through the nRF24L01+ chip if one is connected.  Over serial it behaves as normal.

The nRF24L01+ modules (PCB with the radio chip, antenna and a few other things) can be had
for less than $1 from the likes of ebay.  Add an Arduino clone for $2.50 and it's a wireless
Arduino for $3.50.

Optiboot
========

Optiboot is a minimal Arduino bootloader that takes only around 500 bytes of your Flash memory
instead of the 2 kB the default bootloader needs.  I believe it ships with the Arduino IDE
in more recent versions so you can replace the default bootloader on your Arduino board with
it if you want.  See optiboot/bootloaders/optiboot/README.TXT for more information.

This repository was forked from https://code.google.com/p/optiboot/ using fast-export for
Mercurial to Git conversion.

Building
========

To build a normal optiboot for an atmega328 just run:

    $ make atmega328

To include EEPROM writing support add "SUPPORT_EEPROM=1"

    $ make atmega328 SUPPORT_EEPROM=1

To also add nRF24L01+ support you need to use "LED_START_FLASHES=0 RADIO_UART=1"

    $ make atmega328 LED_START_FLASHES=0 RADIO_UART=1 SUPPORT_EEPROM=1

Your bootloader is ready to burn onto an atmega chip at optiboot_atmega328.hex.  To burn it
you can use another Arduino as described in README.TXT.  LED_START_FLASHES=0 is needed because
the LED uses one of the SPI pins.  BIGBOOT=1 gets enabled automatically.  With both features
enabled the bootloader takes up 1.5 kB instead of the original 0.5 kB.

The nRF chip is expected to be connected to the arduino using the 3 standard SPI pins (MOSI,
MISO, SCK) plus the CE and CSN pins of the nRF chip.  By default optiboot assumes CE is
wired to Analog Pin 1 (PC1) and CSN to Analog Pin 0 (PC0) because they're next to the SPI pins
on some Arduinos.  You can change that mapping in optiboot.c.

Configuring wireless
====================

The radio protocol is the same as serial (STK500v1) so you can use avrdude as normal but you
need another Arduino board with an nRF24L01+ module to work as an adapter between USB /
serial, and the radio packets.  Flash the "flasher" program from
https://github.com/balrog-kun/lights on an Arduino and it will work as such adapter, then
just run avrdude against this board's serial port at 115200 and it will take care of
forwarding the communication to the other board over radio.  That other board must be in
the bootloader at that time, so it needs to be reset at about the same time you run avrdude
or click "Upload" in the Arduino IDE.  There is a possibility for that board to reboot into
bootloader automatically if the app running there supports that.  The flasher automatically
sends a 1-byte 0xff packet before sending the first avrdude command in hope that this will
cause the board to reboot.  But manual reset also works (with the arduino reset button).

Each nRF24L01+ needs a network address.  The protocol uses 3-byte addresses.  Optiboot
reads its nRF24L01+ address from the EEPROM.  The EEPROM bytes 0, 1, 2 (first three bytes
of the whole EEPROM) are read and the contents are used as the board's own address.

Flasher reads the 6 initial bytes of EEPROM, the first three bytes will be used as its
own address (can be whatever), and the second three bytes are the destination address of
the board to be flashed.

You can use avrdude with a command like the following to assign the address 000 (ascii)
to one of the boards, and 001 (ascii) to the other one:

    $ avrdude -F -p atmega328p -P /dev/ttyUSB0 -c arduino -b 115200 -U eeprom:w:0x30,0x30,0x31,0x30,0x30,0x30:m
    $ avrdude -F -p atmega328p -P /dev/ttyUSB1 -c arduino -b 115200 -U eeprom:w:0x30,0x30,0x30,0x30,0x30,0x31:m

Performance
===========

I've tested this mainly indoor with the common 8-pin $1 nRF24L01+ modules with a PCB antenna
(similar to the "Meandered Inverted-F PCB Antenna" type).  I'm able to fairly reliably flash
programs and interact with the bootloader at a 12m distance through various brick walls and
things.  Communication is slightly slower than through serial at 115200 baud.  The antenna
oriantation and polarization doesn't seem to make much difference indoors (this is kind of
confirmed by Texas Instrument application notes about those antennas).

If you have better antennas or the distance is always less than about 5 metres you can disable
some of the mechanisms I added and bump up the nRF24 data rate parameters so that is much
faster than serial, although at that point communication is no longer a bottleneck.

If you need higher distance or work in a noisier radio environment there are a few additional
improvements that can be made for link robustness but if you're losing packets often, most
likely you're already close to the physical maximum range of those radios.
