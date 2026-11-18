# Thread OTA preview — SDK 0.4.9-alpha.2

This branch adds an explicit, admin-operated application update path for the
always-awake C6 Thread test. It does not replace ESPHome, enable automatic
updates, or make arbitrary firmware safe for arbitrary boards.

## User flow

1. Install the C6ThreadOTA receiver once over USB. An old sketch without an OTA
   receiver cannot acquire this feature remotely.
2. In the ET-Core panel choose the Thread connection, open the receiver, and
   select **Firmware update**. Select a reviewed application BIN and enter its
   target firmware version. There is no YAML configuration.
3. Review device, board, version, image size and SHA256. Review sends no commands.
4. **Install firmware** authorizes the exact reviewed image. The hub transfers it
   over the existing encrypted ET-Core port, verifies it, commits, then waits for
   a new matching firmware boot and a separate authenticated health ACK.

The first supported board profile is `esp32c6-dev-4m`, matching the reviewed
C6 counter test. A board token is not proof of sensor wiring or application
correctness. Do not select images for other devices or install unreviewed code.

## Security and resource boundaries

- The upload API requires HA authentication and an administrator. Review plans
  are bound to that administrator, the device, current boot, file hash and
  version. They expire after five minutes. Only an explicit confirmation starts
  a transfer. No remote firmware URLs are fetched.
- Application BIN parsing checks chip ID, flash geometry, application descriptor,
  segments, padding, checksum and appended SHA256. Merged, padded, bootloader,
  malformed and oversized files are rejected. The selected version, board and
  receiver protocol literals must be present in the image. This is a preview
  compatibility gate, not independent code signing or general pin validation.
- One reviewed image and one active transfer per connection. Image upload limit
  4MB, further restricted by the advertised inactive partition capacity. Firmware
  is retained in memory only; history stores metadata, not keys or image bytes.
- 192-byte chunks are base64-encoded inside the ordinary authenticated command
  envelope. One chunk is outstanding at a time. The existing command retries
  retain their command ID; the receiver also checks offset and duplicate bytes.
  Encrypted command and hello sizes were checked against the 1024-byte Thread
  adapter cap. Fragmentation and transfer speed still need radio measurement.
- OTA commands require encryption, a matching transfer ID and board, exact size,
  ordered offsets and complete-image SHA256. The platform backend writes only
  the inactive application partition. It never writes a bootloader, partition
  table, NVS, keys or eFuses.
- Image validation and boot selection are separate operations. Native ESP-IDF
  image validation and the full SHA256 must pass before activation. Receiver
  silence for 60 seconds aborts an incomplete transfer without selecting it.
- Firmware chunk data is omitted from diagnostic event payloads, including
  failed-command events. A firmware binary can contain private credentials.
- Encryption authenticates updates using the provisioned ET-Core key. It is not
  a substitute for independent signing or Secure Boot. Configured platform
  Secure Boot validation remains enforced by ESP-IDF.

## Boot verification and recovery

The reviewed C6 example overrides Arduino's weak `verifyRollbackLater()` hook,
so a pending OTA boot is not accepted during core initialization. The pinned
Arduino-ESP32 3.3.11 C6 build enables bootloader rollback. The new application
must connect, present the expected version in a new authenticated session, and
handle the HA health command before calling the native mark-valid API.

A boot failure/reset while pending uses the bootloader's previous valid slot.
The responsive receiver loop also requests rollback after 180 seconds without
health confirmation. This is not proof that every arbitrary application hang
will trigger a reset; watchdog and failure behavior require physical testing.
The initial USB-installed receiver must be proven healthy before testing OTA.

HA stores update metadata and marks unfinished records **interrupted** after a
restart. It never resumes flashing automatically. If commit may have reached
the receiver but a new boot/health ACK is not proven, status is **unconfirmed**,
not success. Inspect the running version and device before retrying. Native
entities and their IDs are unchanged by this feature.

## Verification completed

- Existing simulator regression: 120 tests passed in 206.781 seconds.
- Latest focused sender/API/IPv6 tests: 36 passed, including admin and explicit
  confirmation, image validation, replay/ACK boundaries, interrupted history,
  storage failure before transfer, new boot/version and health requirements.
- Real shared C++ receiver protocol against injected storage passed duplicate,
  hash, offset, incomplete-image, timeout and plaintext rejection checks; its
  outgoing acknowledgements were decrypted and checked.
- Real ESP32 storage-backend code against host API substitutes passed inactive
  slot, SHA-before-activation, chip/flash mismatch, write/end failure and pending
  boot health/deadline cases. These are host tests, not physical flash tests.
- The exact C6 receiver and separate OTA test build clean-compiled with unchanged
  source for `esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=default`.
  Each uses 1,222,778 program bytes and 87,792 static RAM bytes. The native linked
  `verifyRollbackLater` symbol was checked to be the sketch override.
- Phone (412px) and tablet (1280px) UI checks passed, with all installation calls
  intercepted. Review alone did not install; nonadmin controls were hidden.

The first real Thread firmware transfer is verified below. Power interruption
and hardware rollback are NOT yet verified. No production firmware or installed global library is changed
by opening this branch or installing its HA server code.

## First physical Thread OTA — 18 November 2026

The owner installed receiver 0.3 over USB, then explicitly authorised the OTA
test to 0.4. The real C6 accepted all 1,222,944 application bytes through the
encrypted stop-and-wait protocol, verified the full image before activation,
rebooted into 0.4 in a new authenticated session, and returned the post-boot
health ACK. The updater reported succeeded at 100 percent.

Measured wall time from accepted job to verified health was 862.347 seconds,
about 14.4 minutes, averaging 1418.2 application bytes/s. This conservative
first implementation prioritises ordered writes and per-chunk acknowledgement;
it is not a performance comparison with ESPHome and is not a WiFi-speed claim.
The device kept publishing during the transfer. Failed commands and decryption
failures were zero; this does not imply there were no packet retries.

The application transfer and successful boot/health path now have physical
evidence. No deliberate power cut, corrupted-image flash or physical rollback
test was performed. Configured firmware, device credentials, addresses, image
bytes and raw diagnostic snapshots remain private.
