# C3 isolated sleep test

Experimental SDK 0.4.8-beta.2, dedicated spare ESP32-C3 only. No sensors or
actuators. Counter readings are explicitly marked `test_only`; they are not
weather, soil or battery measurements.

Use an isolated ET-Core hub on UDP **15561** with a **separate test key**. This
example does not use production UDP 5560. Enter WiFi details and that test key
at the top of the sketch. Never commit real credentials or the compiled private
image. The existing `sim/harness.py` provides `make_hub(..., lan=True,
port=15561)` for a supervised hardware test; it needs its matching test key.

## Behaviour

- Cold boot, reset or OTA: stay awake, with ArduinoOTA and HTTP status available.
- OTA hostname `etcore-c3-test`, port 3232, **no OTA password** for this test.
- `GET /status` (or `/`): IP, MAC, firmware, real flash size, free heap,
  wake count, reset reason, session-resume state and test mode.
- `POST /run`: begin ten deep-sleep wakes. Each wake has an eight-second OTA/debug
  window after WiFi connects, then five seconds of deep sleep.
- `POST /wifi-off`: disconnect only this C3 for 35 seconds, then automatically
  reconnect. Cancels sleep first; the router and other clients are untouched.
- `POST /hold`: cancel further sleeps and stay awake; send while the board is awake.
- After ten wakes: automatically stay awake. Every cold reboot also cancels the run.
- Failed WiFi connection: keep retrying; no indefinite setup wait or automatic
  reboot. An invalid test key leaves the device awake for OTA recovery.
- OTA start prevents sleep; a failed upload keeps the device awake.
- Serial at 115200 is mirrored to native USB and UART0 when USB CDC is enabled.
  No sensor/output GPIO is configured. UART0 is used only for diagnostics.

Use an OTA-capable partition layout. The tested compile target is
`esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=default`, with
Arduino-ESP32 3.3.11. Check the real board's flash size before flashing.

This deliberately long awake window is for debugging and OTA access. It is not
an example of optimised battery consumption, and no battery-life estimate can
be derived from it. The documented SDK wake-confirmation replay limitation still
applies; see [battery preview](../../../../../BATTERY-PREVIEW.md).

## Hardware result

Ten timer-based wakes, isolated hub restart, a 19-second hub outage and OTA after
sleep all passed on a 4 MB ESP32-C3. Software reset started a fresh session.
See the [hardware report](../../../../../BATTERY-PREVIEW.md#c3-hardware-test--15-november-2026).
GPIO wake, power-cut and battery-current tests remain outstanding.

Firmware 1.1 also passed the controlled 35-second WiFi-disconnection test;
encrypted leases and OTA recovered without rebooting.
