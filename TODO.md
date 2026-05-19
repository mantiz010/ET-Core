# ET-Core TODO

## Priority 1

- Add command ACK tracking in Home Assistant with retry and timeout handling.
- Add Arduino command ACK helper that includes command sequence and command id consistently.
- Add retained device state in Home Assistant so entities restore cleanly after HA restart.
- Add a clean `bye` packet helper for devices that can announce restart/shutdown.
- Add a 4-channel relay example using ET-Core.
- Add explicit scale test for 50 and 100 simulated devices.

## Priority 2

- Add WS2812 RGB light example with FastLED.
- Add BME280/AHT/SGP30 multi-sensor example.
- Add battery sensor fields and Home Assistant device class/unit mapping.
- Add Home Assistant services for sending raw ET-Core commands and forcing rediscovery.
- Add panel controls for filtering devices, clearing event history, and exporting diagnostics.
- Add configurable lease profile: fast lab mode, normal 50-device mode, and 100-device mode.

## Priority 3

- Add protocol tests for encrypt/decrypt compatibility between Arduino and Python.
- Add replay tests for stale hub commands and stale device state.
- Add simulated device test runner for the Home Assistant hub.
- Add 150-256 device stress test with packet-rate and Home Assistant CPU measurements.
- Add packet capture examples for NetBoss/ETDiag.
- Add release packaging for Arduino library and Home Assistant custom component.

## Scale Requirements

- Must support at least 50 devices.
- Should support 100 devices on normal WiFi/LAN.
- Stress target is 150-256 devices.
- Default lease profile can stay fast at `2s` lease / `6s` TTL.
- 100-device profile should support `3s` lease / `9s` TTL.
- State packets must be on-change plus slow refresh, not constant spam.
- UDP payload should stay under about `1200` bytes.
- Panel needs filtering or pagination before large installs.

## Security

- Keep nonce direction split: device-to-hub is `1`, hub-to-device is `2`.
- Never persist or commit real PSK, WiFi password, or GitHub token values.
- Add optional per-device PSK override.
- Add PSK rotation plan.
- Add documented threat model for LAN spoofing, replay, and denial of service.

## Documentation

- Add wiring diagrams for relay, WS2812, and sensor examples.
- Add Home Assistant install screenshots or notes.
- Add Arduino IDE install notes.
- Add migration notes from ET-Bus to ET-Core.
- Add troubleshooting section for no devices, decrypt failures, and offline flapping.

## Done

- Created ET-Core Git repo.
- Added protocol spec.
- Added Arduino `ETCore` library.
- Vendored existing `ETChaCha20Poly1305` library.
- Added Home Assistant custom component.
- Added switch, light, and sensor entity support.
- Added ET-Core Home Assistant sidebar panel.
- Added diagnostics API.
- Added `RelayOne` example.
