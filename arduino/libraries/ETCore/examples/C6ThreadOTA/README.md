# C6 Thread OTA receiver preview

Always-awake, encrypted Thread counter test with an opt-in OTA application
receiver. First install requires USB. It adds no WiFi, sensors or actuator pins.

Set the existing private Thread dataset, HA IPv6 bootstrap and isolated key at
the top of the sketch. Never publish a configured copy. The public portable
`downloads/C6OTA.zip` includes matching SDK/crypto source; retain `src` and
`build_opt.h`. ArduinoJson 7 and Arduino-ESP32 3.3.11 are external dependencies.
No global library replacement is needed with that archive.

Exact target: ESP32C6 Dev Module, CDC enabled, 4MB, Default partition, Zigbee
disabled, upload/Serial 115200. FQBN:
`esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=default`.
No PSRAM setting or external GPIO is required.

Serial prints the direct HA endpoint, Thread role, IPv6 addresses, EUI64,
readiness, inactive capacity, send failures, uptime and counter. Never use a
merged image or bootloader in the HA Firmware update dialog: it accepts only a
reviewed application BIN for this receiver. Install actions restart the C6.

See [Thread OTA design, safeguards and evidence](../../../../../THREAD-OTA-PREVIEW.md).
This prototype is compiled and host-tested. The first real C6 transfer from
receiver 0.3 to 0.4 succeeded, including new boot and health acknowledgement.
Deliberate power-interruption and physical rollback tests remain pending.

## Speed candidate 0.5

This branch's receiver advertises max_chunk 288 and max_window 2. Use its matching
HA sender, which negotiates this mode and keeps old receivers serial. Missing
begin negotiation fields retain the 192/window 1 default. The faster mode is
host-tested and exact-target compiled; physical speed measurement is pending.
Existing 0.4 can acquire 0.5 over OTA before the separate 0.6 speed benchmark.

The physical 288/window2 experiment failed before activation. The Git sender now
defaults to 192/window1; the receiver remains compatible with that profile. See
[measured results](../../../../../THREAD-OTA-SPEED-PREVIEW.md). No speed gain is claimed.
