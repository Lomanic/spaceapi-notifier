# SpaceAPI notifier

Nodemcu v0.9/v3 + MAX7219 LED matrix display project.

Displays configurable message on the display when the (also configurable) [SpaceAPI](https://spaceapi.io) endpoint state is open.

Initial WiFi configuration is done by connecting to the SPACEAPI NOTIFIER access point and going through WifiManager captive portal.

Custom SpaceAPI endpoint to query, displayed message configuration is then done on http://spaceapi-notifier.local/.

All parameters (WiFi and customizations) can be reset by pressing GPIO0 (flash button on Nodemcu) continuously until the LED_BUILTIN blinks.
You then have to reconfigure the WiFi and custom settings.

## Hardware

* [V3 4M bytes (32Mbits) FLASH NodeMcu Lua WIFI Networking development board Based ESP8266 with firmware](https://www.aliexpress.com/item/32565317233.html)
* [MAX7219 Dot Matrix Module For Microcontroller 4 In One Display with 5P Line](https://www.aliexpress.com/item/32616345083.html)

## Software

Install the Arduino IDE

Needed dependencies (tested with these versions):
* WiFiManager 0.16.0
* MD_MAX72XX 3.3.0
* MD_Parola 3.5.6
