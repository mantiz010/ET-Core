# Hydroponics bucket ET-Core migration — 2026-11-08

## Deployed transport and retained behaviour

The bucket at its existing address runs `8.9-ecfix6-etcore3`, built from the verified `8.9-ecfix6` source. The real hub receives ChaCha20-Poly1305 encrypted telemetry as `hydro-bucket`, profile `hydro_bucket_ecfix6_v1`, using ETCore 0.4.6. MQTT is absent from the deployed gateway transport. Existing web controls, TFT, UART sensor links, Telnet and OTA remain present. No dosing command was issued for testing.

Five original HA IDs were transferred to native ET-Core entities without deleting recorder data. Their names, units and state classes remain unchanged. An additional EC25 sensor in mS/cm was added. A last-day history query returned 10,706 pre-migration records across the five original IDs. The old MQTT YAML sensor entries were removed only after native encrypted readings were verified; unrelated MQTT configuration was semantically unchanged. HA's one-time shadow-statistic rename warnings reflect existing destination statistics; those existing statistics/history were retained.

The EC model header and ten critical measurement/calibration/storage functions are byte-identical to the deployed EC-fixed source. The dosing loop retains its algorithms, setpoints and timings, with an added startup clock-validity gate. The original firmware's unchecked NTP write was replaced with background SNTP: a failed time request no longer overwrites the dosing RTC with an invalid date. The original UTC+12 RTC convention remains unchanged. The original modem-sleep power setting is restored after the SDK starts.

## Evidence

- Original ESP32, 4 MB, PSRAM disabled, default OTA application slots; Arduino-ESP32 3.3.10, exact original FQBN.
- Clean, unchanged-source compile: 1,170,305 program bytes, 77,368 static RAM bytes.
- Application image: 1,170,448 bytes, original ESP32 chip ID 0, fits the 1,310,720-byte OTA slot. The running sketch MD5 matches this exact image.
- 1,640 EC model/filter/freshness/corruption checks with ASan/UBSan.
- 45 extracted calibration/storage handler checks, including failed writes, reboot, maintenance/OTA gates and counter restoration.
- 12 HA adapter tests covering type/validity checks, stale state, reconnect/reboot, profile checks and removal/recreation; additional extracted production-clock checks passed.
- Emitted device web JavaScript and ET-Core panel JavaScript syntax checks passed.
- Real device: valid retained two-point EC calibration, valid raw EC/temperature, valid RTC date, original Wi-Fi power mode, and both dose counters restored after clock/day initialisation.
- Final hub snapshot: all eight real ET-Core devices online; zero decrypt failures and zero replay drops.
- Live Serial/Telnet/web features remain in source; Telnet output and web/data/debug endpoints were read on the real device. TFT pixels and actual pump actuation were not physically tested.

The source, original verified application image, HA configuration/registry, active MQTT include, calibration values, setpoints and counters were backed up privately before deployment. Application-only OTA retained the existing partition layout and EEPROM/NVS. This is not a fresh full-flash/NVS readback. Daily counters retain the original reboot/reset behaviour and were explicitly restored; transient startup/reset states can occur during cutover and are not a new permanent counter journal.

## Commissioning hold: verify pH before dosing

The conversion and stored pH calibration were not changed. However, the displayed pH changed from about 5.9 before reboot to different values after reboot. A maintenance-only comparison with the **exact original application image** also produced a changed reading (about 7.4 after its sample run). The final ET-Core image was then restored and verified. This establishes that the original firmware also exhibits the observed post-reboot variation; it does not establish the actual solution pH or a physical cause.

**Maintenance remains ON and dosing outputs are held off pending an independent pH meter check.** Do not change calibration coefficients to force a desired reading. A current physical pH reading is needed before commissioning automatic dosing. The original setpoints, automatic-dose settings, limits and counts are preserved for that step. EC telemetry migration is verified; physical pH accuracy and dosing are not signed off.

See the [example setup and recovery instructions](../arduino/libraries/ETCore/examples/HydroBucket/README.md) and [reusable regression tests](../tests/hydro_bucket/README.md).
