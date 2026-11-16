# C5 ET-Core connectivity test

Spare ESP32-C5 only: always awake, no sensor or actuator GPIO. Dummy counter
readings are marked test_only. Enter WiFi, OTA password and a separate test key
at the top of C5Test.ino. Uses isolated UDP 15561; configure a separate ET-Core
Home Assistant entry with that key. Never use a production encryption key.

Serial 115200 mirrors native USB and UART0: IP, MAC, RSSI, channel and uptime
every five seconds. OTA hostname etcore-c5-test, port 3232. HTTP /status shows
firmware, IP/MAC, channel, RSSI, measured flash/PSRAM, counter and OTA readiness.
The counter advances every five seconds and resynchronises after hub discovery.

Verified clean build with explicit ETCore SDK 0.4.8-beta.2, ArduinoJson and
ETChaCha20Poly1305 from the battery-review branch; Arduino-ESP32 3.3.11.
Target esp32:esp32:esp32c5:CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=default.
Physical C5 verified with 4MB flash, no PSRAM, 2.4GHz channel 6: OTA upload,
HTTP status, fresh encrypted counter and HA Wi-Fi health profile passed.
5GHz, long-duration reliability and deliberate outage tests are not yet verified.
