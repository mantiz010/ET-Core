# Battery / deep-sleep preview — SDK 0.4.8-beta.1

Experimental development branch based on SDK/hub 0.4.7. Not a production release.
The Home Assistant integration is unchanged. Do not replace an installation
containing newer hub features with the integration folder from this branch.

## Included

- RTC-backed session and transmit sequence restoration after ESP32 deep sleep.
- Extended leases via `setSleepInterval(seconds)`.
- Bounded hub wait, then sleep even when the hub is unavailable.
- Resynchronisation after hub restart; cached last state up to 512 bytes.
- Soil-moisture and door-contact examples with placeholder credentials.

## Limits before hardware rollout

- Physical RTC retention, GPIO wake, current draw and battery life remain untested.
- The soil example defaults to GPIO34 on classic ESP32. Compilation on another
  family does not validate that pin. Select a real ADC-capable pin for the board.
- Door-contact wake wiring must be checked against the exact board; GPIO4 is
  the example default, not a promise of compatibility with every ESP32 module.
- Commands are not queued during sleep. Sleeping actuators are not supported
  by this preview's command-delivery model.
- `waitForHub()` confirms receiving a hub packet, not a durable acknowledgement
  that every reading was stored. A recorded announce for the same session can
  falsely confirm a wake. Replay-resistant wake confirmation remains required
  before treating this as reliable delivery.
- The hub rate-limits nohub replies to one per device per second. Rapid repeat
  wakes can deliver their reading but wait until timeout without confirmation.
- Lease TTL is capped at 65535 seconds (about 18.2 hours); the three-missed-report
  rule cannot hold above a sleep interval of 21841 seconds. A sleep longer than
  the TTL can appear offline before the next wake.
- Use `onSync()` for state larger than 512 bytes or multiple independent readings;
  the automatic cache keeps only the most recent `sendState()` payload.
- Configure a real shared encryption key before use. Blank example keys are
  placeholders, not a secure deployment configuration.

## Next

1. Add replay-resistant wake confirmation and regression coverage.
2. On a designated test board: cold boot, ten timed wakes, hub restart between
   wakes, power loss, WiFi outage and GPIO wake. Measure actual awake time/current.
3. Only after those checks, promote the SDK into the firmware build environment.
   Installing this Git branch does not update already-flashed devices.

Run the isolated host suite with `sh sim/run-tests.sh`. No live hub is needed.

## Review evidence — 15 November 2026

- Isolated simulator: 51 tests passed (201.330 seconds).
- Arduino-ESP32 core 3.3.11, explicit candidate SDK and crypto library paths.
- Clean builds with unchanged sketch source: `SleepySoilMoisture` on
  `esp32:esp32:esp32`; `SleepyDoorContact` on `esp32:esp32:esp32`,
  `esp32:esp32:esp32c3`, `esp32:esp32:esp32s3`; `RelayOne` on
  `esp32:esp32:esp32`. Board menu defaults were used; these are generic
  compile targets, not validation of a particular physical board or wiring.
- The simulator emitted subprocess-stream ResourceWarnings; assertions passed.
- No physical devices flashed, no installed build libraries replaced, and no
  live Home Assistant changes made by this review.
