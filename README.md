# Mobile-Air-Sensor
Build a low cost air quality sensor. Dust, humidity, temperature.

Mobile sensor derived from the Luftdaten design. The code is all new but the hardware is almost the same: SDS011, esp8266 (Nodemcu), DHT22, case differs.
This started out as a mains powered version which could operate remotely (no WiFi signal) and store a week’s readings in EEPROM using the SPIFFS file system.
It boots up into station mode to allow FTP transfer of data, deletion of the downloaded file and input of new date/time to start a new file.
It then takes one set of sensor readings, displays them to prove working and shuts off WiFi to avoid broadcasting the presence of the unit.
If power is interrupted it will reboot, so there is a 5 minute timeout on setup after which sensing continues, except without a date/timestamp. The timeout increases to 20 minutes once setup commences and appears ample to FTP a week of data.

An ESP8266 is not the best choice for a battery powered version but it’s easy to make a cheap portable version using a Li-on 18650, charger/protection board, step up converter, and small food box.
Ideally it should run for 24 hours but as it stands the SDS011 takes 70mA during the warmup and reading, and the nodemcu and other circutry is taking another 70mA giving an average for a 60s cycle with 30s warmup 0f 105mA. There’s additional power used in the step up converter which I’ve not measured but suffice to say it lasts well short of 24 hours at the moment.

I'm not putting the nodemcu to sleep (thinking it would lose the date/time when deep sleeping) but have since realised it
can get the date/time by retrieving the previous record from SIPPS and adding one cycle time (one minute currently). I am also in the process of altering the set up to customise cycle and warmup times. With a small inlet pipe and being mobile I wonder if a shorter warmup time is acceptable. It will be easier to experiment if it can be changed on reboot.
More batteries is another possibility but I want to keep it light and small.
