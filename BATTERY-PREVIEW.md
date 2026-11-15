# Battery / deep-sleep preview — SDK 0.4.8-beta.2

Experimental development branch based on SDK/hub 0.4.7. Not a production release.
The hub now includes sleep-protocol 2 changes. Do not replace an installation
containing other newer hub features with this whole integration folder; merge
the targeted changes instead. The manifest remains at its 0.4.7 base version, so
check `sleep_protocol: 2` in diagnostics to identify this development build.

## Included

- RTC-backed session and transmit sequence restoration after ESP32 deep sleep.
- Extended leases via `setSleepInterval(seconds)`.
- Bounded hub wait, then sleep even when the hub is unavailable.
- Resynchronisation after hub restart; cached last state up to 512 bytes.
- Soil-moisture and door-contact examples with placeholder credentials.

## Limits before hardware rollout

- ESP32-C3 timer wake and RTC session retention passed the hardware test below.
  GPIO wake, other chip families, current draw and battery life remain untested.
- The soil example defaults to GPIO34 on classic ESP32. Compilation on another
  family does not validate that pin. Select a real ADC-capable pin for the board.
- Door-contact wake wiring must be checked against the exact board; GPIO4 is
  the example default, not a promise of compatibility with every ESP32 module.
- Commands are not queued during sleep. Sleeping actuators are not supported
  by this preview's command-delivery model.
- SDK beta.2 requires a unicast reply echoing a fresh random token generated for
  this wake. Use the updated hub (diagnostics `sleep_protocol: 2`) and encryption
  for authenticated confirmation. Replayed replies from earlier wakes cannot
  confirm a new wake. This is still not a durable acknowledgement of every reading.
  An older hub can receive readings, but the new SDK times out rather than claiming
  fresh confirmation without the token echo.
- The hub rate-limits nohub replies to one per device per second. Rapid repeat
  wakes can deliver their reading but wait until timeout without confirmation.
- Lease TTL is capped at 65535 seconds (about 18.2 hours); the three-missed-report
  rule cannot hold above a sleep interval of 21841 seconds. A sleep longer than
  the TTL can appear offline before the next wake.
- Use `onSync()` for state larger than 512 bytes or multiple independent readings;
  the automatic cache keeps only the most recent `sendState()` payload.
- Configure a real shared encryption key before use. Blank example keys are
  placeholders, not a secure deployment configuration.

## OTA starter

Before the battery test, USB-flash the always-awake [C3OTA example](arduino/libraries/ETCore/examples/C3OTA).
[Download C3OTA.zip](https://github.com/mantiz010/ET-Core/raw/refs/heads/feature/battery-review/downloads/C3OTA.zip).
It uses only the ESP32 core libraries; enter WiFi and OTA credentials in the sketch.

## Next

1. Replay-resistant wake confirmation and regression coverage are implemented.
2. Extend the C3 timer-wake test to physical power loss, WiFi access-point outage
   and GPIO wake. Measure actual awake time/current without the debug window.
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

## C3 hardware test — 15 November 2026

- Dedicated ESP32-C3, actual flash size 4 MB; SDK 0.4.8-beta.1, Arduino core 3.3.11.
- Encrypted test traffic used a separate key and isolated UDP port 15561.
  Production Home Assistant, its entities and installed firmware libraries were
  not modified.
- Ten timer-based deep-sleep wakes passed (wake counters 1–10), with RTC session
  restoration on each wake and the same device session throughout the test.
- Isolated hub restart recovered; a 19-second hub outage also recovered.
  An outage can lose individual readings; this is recovery, not guaranteed delivery.
- Supervised run took about 158 seconds with deliberate eight-second awake/OTA
  windows and five-second sleeps. This is not a battery-current measurement.
- Board automatically stopped sleeping at wake 10. A second OTA upload then
  succeeded without a password. Software reboot started a new session and left
  it awake with OTA available.
- Physical power removal, GPIO wake, WiFi AP outage, current consumption and
  battery lifetime were not tested. The known announce-replay limitation remains.
- [C3SleepTest source and operation](arduino/libraries/ETCore/examples/C3SleepTest).
  A known OTA starter rollback image and private test evidence were retained
  locally; credentials and device addresses are excluded from this repository.

## Wake confirmation and sleep display — SDK beta.2

- Added a fresh random per-wake token, echoed inside the encrypted unicast hub
  reply. The reproduced replay regression fails before the fix and passes after.
- Final leases explicitly report `sleeping_for`. Ordinary awake leases clear that
  intent. The hub exposes `sleep_state`, `sleep_interval`, and `next_report_in`.
- Panel cards, tables and details distinguish Sleeping, Report due, Online and
  Offline. Report due does not shorten the existing offline lease allowance.
- Verified 54 simulator tests plus a targeted older-hub compatibility test;
  three targeted tests also passed against the staged extended live hub.
- Phone/tablet panel checks passed at 412 and 1280 pixels, including powered-device
  fallback and no horizontal overflow in the sleep notice.
- Revised C3 firmware passed ten timer wakes, isolated hub restart, a 19-second
  hub outage, and a 35-second disconnection of only the spare C3's WiFi. Encrypted
  reporting and OTA recovered after WiFi returned (about 38 seconds total).
- Production gateway firmware and the installed Arduino build library are not
  updated by these changes. Physical power removal and current measurement are
  separate checks, not inferred from the simulator or software resets.

Live deployment check: targeted hub/panel changes were applied after backing up
the integration and entity/device registries. All 216 existing ET-Core entity IDs
and unique IDs were preserved, and all eight previously-online devices returned
after the Home Assistant restart. The already-offline scanner was not changed.
Existing live log labels and the migration dialog passed regression checks at
phone and tablet widths. No real device credentials are included in this branch.

### Physical power-loss verification — 15 November 2026

The user disconnected and restored USB power on the spare C3. Verification confirmed a power-on reset, cleared RTC wake state, a new encrypted session, fresh test readings received by the isolated hub, and OTA still enabled while held awake. This completes the physical cold-start check; electrical current consumption remains unmeasured.
