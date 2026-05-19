# ET-Core

ET-Core is a local-first UDP device protocol for fast ESP32-to-Home Assistant control.

It is not ET-Bus v1. ET-Bus stays as the working legacy protocol. ET-Core starts clean with lease-based availability, command acknowledgements, and fixed crypto rules from day one.

## Current Version

ET-Core is currently at the first working code pass, `0.2.0`.

The repo includes:

- Arduino `ETCore` library.
- Vendored `ETChaCha20Poly1305` Arduino crypto library.
- Home Assistant custom integration.
- Home Assistant sidebar web panel.
- First relay example.
- Protocol spec.

## Goals

- No hub ping/pong loop.
- Fast offline detection using device leases.
- State updates only on change, plus slow refresh.
- Commands acknowledged by the device.
- Home Assistant availability follows lease expiry.
- ESP32 firmware remains simple and fast.
- Security design avoids nonce reuse across reboot.
- Support at least 50 devices, with a design target of 100 devices.

## Protocol Shape

- `hello`: device metadata after boot.
- `lease`: small online proof every few seconds.
- `state`: device state on change and periodic refresh.
- `cmd`: Home Assistant to device.
- `ack`: device confirms command.
- `bye`: optional clean shutdown/restart notice.

Default timing:

- Lease interval: `2s`
- Lease TTL: `6s`
- State refresh: `60s`
- Command ACK timeout: `300ms`
- Command retries: `2`

For larger networks around 100 devices, use a `3s` lease interval and `9s` TTL if WiFi airtime or Home Assistant load becomes noisy.

## Scale Target

ET-Core is designed to support:

- Minimum target: `50` devices.
- Normal design target: `100` devices.
- Stress-test target: `150-256` devices.

Rules for 100-device operation:

- Devices send `lease` packets only at the configured interval.
- Devices send `state` on change, plus a slow refresh.
- UDP payloads should stay under about `1200` bytes.
- Sensors should use compact field names and avoid large JSON bodies.
- Home Assistant keeps device state in one hub table and updates entities from that table.
- The web panel must support filtering or pagination before ET-Core is considered production-ready for 100+ devices.

## Encryption

Encrypted mode is optional.

When enabled, ET-Core uses the existing `ETChaCha20Poly1305` Arduino library and Python `cryptography` on the Home Assistant side.

Rules:

- Device key is `sha256(master_psk || device_id)`.
- Device-to-hub nonce direction byte is `1`.
- Hub-to-device nonce direction byte is `2`.
- Each sender includes fresh session bytes in the nonce.
- Each sender uses a monotonic sequence number in the nonce.

This avoids the ET-Bus v1 reboot nonce-reuse problem.

## Repository Layout

- `spec/`: protocol documents.
- `arduino/libraries/ETCore/`: Arduino library.
- `arduino/libraries/ETChaCha20Poly1305/`: vendored ChaCha20-Poly1305 library.
- `homeassistant/custom_components/etcore/`: Home Assistant custom integration.
- `homeassistant/custom_components/etcore/www/`: Home Assistant panel UI.
- `examples/`: known-good starter projects.
- `TODO.md`: development roadmap.

## Arduino Install

For Arduino IDE or `arduino-cli`, make both libraries available:

- `arduino/libraries/ETCore`
- `arduino/libraries/ETChaCha20Poly1305`

Example compile command from this repo:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries /home/mantiz010/projects/ET-Core/arduino/libraries /home/mantiz010/projects/ET-Core/examples/RelayOne
```

## Home Assistant Install

Copy this folder into Home Assistant:

```text
homeassistant/custom_components/etcore
```

Then restart Home Assistant and add `ET-Core` from integrations.

The sidebar panel is registered at:

```text
/et-core
```

The diagnostics API is:

```text
/etcore/api/diag
```

## First Example

The starter firmware is:

```text
examples/RelayOne/RelayOne.ino
```

Set your WiFi values before flashing.

To enable encryption, set `ETCORE_PSK_HEX` to a 64-character hex key and enter the same key in the Home Assistant ET-Core config flow.

## Validation

Last verified:

- Home Assistant Python syntax compile passed.
- `RelayOne` compiled for `esp32:esp32:esp32`.
- No `StaticJsonDocument` usage.
- No real WiFi password or GitHub token patterns in the repo.

## Status

Working v0.2 code pass:

- Arduino `ETCore` library with hello, lease, state, command, ACK, and encrypted payload support.
- Vendored `ETChaCha20Poly1305` library is used for ESP32 ChaCha20-Poly1305.
- Home Assistant custom integration with switch, RGB light, and dynamic sensor entities.
- Optional PSK can be entered in the Home Assistant config flow.
- `examples/RelayOne` compiles on `esp32:esp32:esp32`.

See `TODO.md` for the next work.
