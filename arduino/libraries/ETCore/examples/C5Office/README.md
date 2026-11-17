# Office BLE presence preview

ESP32-C5 receiver for a phone advertising an iBeacon, such as Home Assistant
Android Companion BLE Transmitter. This estimates proximity to one receiver;
it does not establish exact room location or detect people without a beacon.

Uses the same c5-ble-presence device ID as C5BLE, preserving existing gateway
entities. Adds room, presence_status, office_present and signal diagnostics.
Absent tracking configuration is reported as unconfigured, not an occupied room.
No automations are installed.

Enter WiFi, OTA password and isolated test key in the sketch. Test UDP 15561.
Build target: esp32:esp32:esp32c5:CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=no_fs.
Arduino-ESP32 3.3.11 and explicit ETCore beta.2 / crypto / ArduinoJson libraries.
Requires the larger No FS (2MB APP x2) layout already used by C5BLE. Do not
OTA-upload into the smaller starter layout. No sensor or antenna GPIO added.

Serial 115200 prints IP, MAC and WiFi/OTA status every five seconds.
HTTP / shows a phone-friendly beacon picker and manual UUID:major:minor input.
/nearby lists up to 24 advertisers; /status shows gateway diagnostics.
POST /track saves the chosen iBeacon identity; an empty identity clears it.
POST /thresholds saves enter/exit RSSI values. Configuration persists in NVS.
The page is for a trusted test LAN. BLE addresses are shown only locally;
beacon UUIDs/MACs are not automatically published into Home Assistant.

iBeacon identity is decoded from manufacturer data, independently of rotating
MAC addresses. RSSI uses an exponential moving average, alpha 0.25. Starting
thresholds are -65 dBm to enter after three seconds and -75 dBm to leave after
15 seconds. At least two recent advertisements are required. No advertisement
for 30 seconds clears proximity. Starting thresholds are not calibrated: test
inside/outside the room, screen locked and phone in a pocket before automating.

Host tests cover identity parsing, malformed data, signal hysteresis, absence
and millis rollover. Browser fixtures cover selection, clearing and viewport
fit at 412 and 1280 pixels. Real S25 identification and office boundary accuracy
remain unverified until the owner enables the phone BLE transmitter.
