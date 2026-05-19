# ET-Core TODO

## Priority 1

- Add command ACK tracking in Home Assistant with retry and timeout handling.
- Add Arduino command ACK helper that includes command sequence and command id consistently.
- Add retained device state in Home Assistant so entities restore cleanly after HA restart.
- Add a clean `bye` packet helper for devices that can announce restart/shutdown.
- Add a 4-channel relay example using ET-Core.

## Priority 2

- Add WS2812 RGB light example with FastLED.
- Add BME280/AHT/SGP30 multi-sensor example.
- Add battery sensor fields and Home Assistant device class/unit mapping.
- Add Home Assistant services for sending raw ET-Core commands and forcing rediscovery.
- Add panel controls for filtering devices, clearing event history, and exporting diagnostics.

## Priority 3

- Add protocol tests for encrypt/decrypt compatibility between Arduino and Python.
- Add replay tests for stale hub commands and stale device state.
- Add simulated device test runner for the Home Assistant hub.
- Add packet capture examples for NetBoss/ETDiag.
- Add release packaging for Arduino library and Home Assistant custom component.

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
