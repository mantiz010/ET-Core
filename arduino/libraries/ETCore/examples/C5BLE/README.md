# C5 BLE presence gateway (preview)

Passive BLE advertising scanner for an ESP32-C5, with encrypted ET-Core reports,
ArduinoOTA and Serial diagnostics. No external GPIO or antenna switch is assumed.

**One USB upload is required to change from the small starter partition layout.**
Board ESP32C5 Dev Module; USB CDC enabled; Flash Size 4MB; PSRAM disabled;
Partition Scheme **No FS (2MB APP x2)** (`no_fs`). Keeps two OTA slots.
Exact target: esp32:esp32:esp32c5:CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=no_fs.
Arduino-ESP32 3.3.11; explicit ETCore beta.2, ETChaCha20Poly1305 and ArduinoJson
from the battery-review build environment. BLE, Preferences and WebServer are
provided by the ESP32 core.

Enter WiFi, OTA password and a separate test encryption key at the top of the
sketch. UDP 15561 matches the isolated test HA entry. Device ID c5-ble-presence,
class sensor.ble_presence. Do not use a production encryption key in a test.

Serial 115200 mirrors native USB and UART0, showing IP/MAC and WiFi/OTA status
every five seconds. Open the printed IP in a browser. The page lists up to 24
recent BLE advertiser addresses, names, RSSI and age. Select Track to persist
one address in NVS; Clear tracking removes the selection. Uses passive
three-second scans on a five-second cadence with asynchronous completion.

ET-Core reports nearby_devices, tracking_configured, tracked_present,
tracked_age_seconds and tracked_rssi when applicable, plus gateway diagnostics.
Presence means last advertisement within 30 seconds; RSSI is not distance.
Unconfigured tracking does not create a claimed occupied room. Phones can
rotate addresses; prefer a beacon with a known stable identity. No person is
automatically identified. The page is intended for a trusted local test LAN.

HTTP: GET /, /nearby, /status; POST /track with form field mac.
OTA hostname etcore-c5-test, port 3232. The existing connectivity test device
will go offline when this replaces it; the BLE gateway uses a distinct ID.

The 1.3MB starter slots cannot hold this roughly 1.59MB firmware. Do not attempt
to apply it as an ordinary OTA update on the starter layout.
Build and UI validation do not establish physical BLE detection or presence
accuracy; those require a real beacon and observation after the USB upload.
