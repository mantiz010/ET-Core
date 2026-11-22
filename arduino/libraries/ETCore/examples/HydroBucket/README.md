# Hydroponics bucket: EC-fixed ET-Core migration

This example preserves the existing 8.9-ecfix6 controller, including its paired raw/temperature two-point EC model, NVS namespace `bucket_ec6`, legacy EEPROM addresses, pH conversion, dosing loop, time slots, maintenance interlocks, TFT menus, web controls, Telnet and Arduino OTA. It replaces the MQTT telemetry connection with encrypted ET-Core on UDP 5560. It adds no remote dosing command. A startup clock defect found during hardware verification is also repaired: background SNTP replaces the unchecked blocking NTP write, invalid startup RTC dates block dosing, and the original UTC+12 RTC convention is retained. Existing local/web control routes remain available.

## Exact installation used for verification

- Original ESP32, 240 MHz, QIO 80 MHz, 4 MB flash, default OTA partitions, PSRAM disabled; Arduino-ESP32 3.3.10.
- FQBN: `esp32:esp32:esp32:CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,LoopCore=1,EventsCore=1,DebugLevel=none,EraseFlash=none,JTAGAdapter=default`.
- ETCore 0.4.6 and its ETChaCha20Poly1305 library; ArduinoJson 7 API.
- TFT_eSPI 2.5.43 with the existing `Setup42_ILI9341_ESP32.h` selected. Existing SerialTransfer 1.2.2, RTClib 1.2.0 and NTPClient 3.2.1 dependencies are retained.
- UART1 RX16/TX17 and UART2 RX25/TX26; pH pump GPIO5 and nutrient pump outputs GPIO32/33. All original pin assignments are unchanged. This is a migration of that exact existing installation, not a pin allocation template for a new board. Keep its established TFT/button wiring and external pulls.

Copy `bucket_credentials.example.h` to `bucket_credentials.h` and fill in local Wi-Fi and the existing ET-Core site key. The configured header and firmware binaries must stay private. The identity is `hydro-bucket`, class `sensor.hydro_bucket`, profile `hydro_bucket_ecfix6_v1`; change the identity for additional controllers. OTA retains the existing `ecph-controller` hostname and the original OTA configuration.

The background task owns ET-Core and retries Wi-Fi at 15-second intervals. After ET-Core starts, it restores the original Wi-Fi power-saving mode rather than adopting the SDK's no-sleep default; this preserves the existing analogue controller power behaviour. Leases are 2 seconds with a 10-second TTL; state is sent every 9 seconds and on hub resynchronisation. Invalid EC/CF or raw temperature is sent explicitly as null, never a fabricated zero. `etcore_sender_ready` in `/data` means the encrypted sender is ready; use HA diagnostics to verify hub reception.

## Home Assistant cutover

Install the `hydro_bucket.py` adapter and its `sensor.py` setup hook, retaining any other local ET-Core adapters. Restart Home Assistant to load changed Python modules. The adapter creates six sensors and requires the advertised profile. It rejects nonnumeric/boolean/nonfinite measurements and invalid counters; EC validity gates EC/CF and raw validity gates temperature. Readings expire after 30 seconds even if leases continue, and old replayed state does not become fresh.

Back up the active MQTT include, registry and original firmware before removing only the five bucket MQTT sensor entries. Verify encrypted native readings first, reload MQTT to release the old IDs, then assign the native registry entities these exact IDs:

- `sensor.hydro_bucket_system_cf`
- `sensor.hydro_bucket_system_ph`
- `sensor.hydro_bucket_system_cf_dose_count`
- `sensor.hydro_bucket_system_ph_dose_count`
- `sensor.hydro_bucket_tank_temperature`

Names, units and state classes are preserved. The EC25 reading additionally has `sensor.hydro_bucket_system_ec` in mS/cm. Do not delete recorder history. Existing dashboard/web control routes need no transport change.

Before OTA, save maintenance mode ON and verify all pump-active flags are false. Capture calibration, settings and daily counters immediately before upload. Application-only OTA with the same partition layout retains EEPROM/NVS. After boot, verify the exact sketch MD5 and unchanged calibration/settings, restore the captured counters through the maintenance-only restore endpoint, and check live EC/temperature readings before returning maintenance to its previous setting. Never test this migration by dosing. The saved rollback application is private; it is the previously verified image, not a fresh full-flash/NVS readback.
