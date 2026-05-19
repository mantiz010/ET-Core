# ET-Core

ET-Core is a local-first UDP device protocol for fast ESP32-to-Home Assistant control.

It is not ET-Bus v1. ET-Bus stays as the working legacy protocol. ET-Core starts clean with lease-based availability, command acknowledgements, and fixed crypto rules from day one.

## Goals

- No hub ping/pong loop.
- Fast offline detection using device leases.
- State updates only on change, plus slow refresh.
- Commands acknowledged by the device.
- Home Assistant availability follows lease expiry.
- ESP32 firmware remains simple and fast.
- Security design avoids nonce reuse across reboot.

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

## Repository Layout

- `spec/`: protocol documents.
- `arduino/libraries/ETCore/`: Arduino library.
- `homeassistant/custom_components/etcore/`: Home Assistant custom integration.
- `examples/`: known-good starter projects.

## Status

Initial scaffold. Do not deploy to production yet.
