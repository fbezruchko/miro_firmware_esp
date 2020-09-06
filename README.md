# Miro ESP8266 firmware (for Arduino IDE).

This is a fork of Arduino.org WiFi Link firmware by jandrassy (https://github.com/jandrassy/arduino-firmware-wifilink). The goal of this fork is make special improvements for Miro robot. Its also clean some unused code for Miro robot.

The main improvement is integrated telnet bridge on port 23, which allows to debug arduino part over WiFi. But there is no OTA firmware upload to ESP8266.
Also, by default, GPIO pin for Arduino Reset defined to GPIO2 (on ESP8266, pin D4 on Wemos-D1-mini).

