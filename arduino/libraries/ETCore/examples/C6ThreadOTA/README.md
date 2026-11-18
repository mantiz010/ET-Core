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
This prototype is compiled and host-tested; physical OTA/rollback remain pending.
