# Mobile-Air-Sensor
Build a low cost air quality sensor: dust, humidity, temperature. Battery current is around 40mA on average for a 60s cycle with 30s warm up. This used a 18650 but 10 hours is around 400mAh so a smaller battery could be used. A 10cm x 8cm food box was used.

This started out as a mains powered version, derived from the Luftdaten design ( https://luftdaten.info/en/construction-manual/ ), but which could operate remotely (no WiFi signal) and store a week’s readings in EEPROM using the SPIFFS file system. It boots up into AP mode to allow FTP transfer of data, deletion of the downloaded file and input of new date/time to start a new file. The IP address is printed to the Serial Monitor but should be 192.168.4.1.

The cycle and warm up times can be changed at this point and a percentage adjustment to the internal clock can be supplied if it's important to have an accurate timestamp.

The code is all new but the hardware is almost the same: SDS011, esp8266 (Nodemcu), DHT22, but case differs. For deep sleep to operate the D0 pin must be connected to the RST pin after flashing the code. The flash size for SPIFFS has to be set in the Arduino IDE Tools Menu. See the image. 

It then takes one set of sensor readings, displays them to prove working and shuts off WiFi to avoid broadcasting the presence of the unit. If power is interrupted it will reboot, so there is a timeout on setup (see code) after which sensing continues, except without a date/timestamp. The timeout increases to 20 minutes once setup commences and appears ample to FTP  data.

An ESP8266 is not the best choice for a battery powered version but it’s easy to make a cheap portable version using a Li-on 18650, charger/protection board, step up converter, and small food box. 

Parts:
SDS011 air particle sensor
DHT11 (or DHT22 which is better) humidity and temperature sensor
Micro USB 5V 1A 18650 TP4056 Lithium Battery Charger Module
2-24V to 2-28V 2A DC-DC SX1308 Step-UP Adjustable Power Module Step Up 

A hot glue gun was used to fix the charger and step up modules to the battery. I'm waiting on a switch arriving so the on/off is by wire/pin connection in the photos.

Libraries:
https://github.com/nailbuster/esp8266FTPServer in addition to the Luftdaten ones.

A simpler version would use a single cell powerbank incorporating the charger and 5v step up circuitry and sockets. A larger or custom made box would be required.
