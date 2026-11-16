# C5 OTA starter

Always-awake USB-first starter for a spare ESP32-C5. No sensors, actuators,
ET-Core hub registration or changes to another scanner. Uses only core libraries.

Open `C5.ino` and enter WiFi and OTA settings at the top of the sketch.
Select **ESP32C5 Dev Module**, **USB CDC On Boot: Enabled**, **Flash Size: 4MB**,
**PSRAM: Disabled**, and **Partition Scheme: Default (OTA capable)**.
These use a 4MB layout matching the saved generic C5 scanner target; this does
not prove the physical flash capacity of another board. Boot prints measured
flash and PSRAM sizes. No sensor pins or antenna-switch GPIOs are configured.

Serial Monitor: **115200 baud**. Diagnostics are mirrored to native USB and
UART0. Every five seconds the sketch prints connection progress or IP, MAC,
RSSI, channel, uptime and OTA status. It never waits indefinitely for Serial
or WiFi. A lost connection retries every 15 seconds.

OTA hostname: `etcore-c5-test`, port **3232**, using the password in the sketch.
It stays awake for future updates. Keep its device identity separate from an
existing RF scanner. This starter does not yet perform the 5GHz ET-Core test.

Build target: `esp32:esp32:esp32c5:CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=default`
with Arduino-ESP32 **3.3.11**. Hardware operation requires the initial USB upload.
Public copies contain placeholders only; never commit a configured private copy.
