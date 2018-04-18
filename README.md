# fpgaupload
Esp8266 web server upload to Upduino V1.0 FPGA

This is the main program for a Platformio project for the Wemos D1 Mini connected to a Upduino V1.0 FPGA board.
Apart from the HSPI connections the CRESET and SS connections are connected to GPIO2 and GPIO16 respectively.
Right now only a .hex file is supported.
The J2 jumper needs to be shorted to upload to the SPI Flash chip.

This code provides an examples of programming a SPI flash from a web interface with an ESP8266 and also how to switch between loading the flash and booting the FPGA.

