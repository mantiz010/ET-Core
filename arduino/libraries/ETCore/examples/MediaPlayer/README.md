# ESP32-WROVER native ET-Core media player

Read [the wire contract and migration gates](../../../../../docs/media-player.md).
This is a generic example, not a configured personal firmware download.

- Board: ESP32 Wrover Module; PSRAM required.
- Core: Arduino-ESP32 3.3.6.
- Partition: Minimal SPIFFS (1.9 MB APP **with OTA**), not Huge APP.
- Flash: saved 4 MB target, DIO/80 MHz; upload/serial 115200.
- MAX98357A: BCLK 26, LRC 25, DOUT 22. No SD card required.
- Dependencies: ETCore and ETChaCha20Poly1305 from this repository,
  ArduinoJson 7, and the tested ESP32-audioI2S library. The legacy audio header
  used for testing identifies itself as 3.0.3a (package metadata 2.0.0); its
  Vorbis `mdct_unroll_lap` signature consistently uses `int32_t*`. An arbitrary
  library version is not equivalent. Personal deployment bundles must pin the
  tested dependency files so the Windows installed copy is not selected.

Enter Wi-Fi details, your ET-Core encryption key, a unique device ID, and a
separate strong OTA password in the sketch. Never commit these values. The
transport fails closed for an invalid key and OTA remains disabled for its
placeholder password. The first USB upload must install the OTA partition
layout; later updates use the password-protected ArduinoOTA network port.

Do not upload to a board with different wiring without reviewing its pin map.
Do not erase a working device before saving and verifying its current firmware.

The included `partitions.csv` pins the tested dual-slot OTA layout. Keep it with
the sketch. Arduino-ESP32 3.3.6 copies a sketch-local partition table on both
Windows and Linux. Image size must remain below 1,966,080 bytes for either slot.

### HTTP file pause compatibility

The tested audio dependency needs the accompanying `audio-http-file-pause.patch`
when its `Audio::pauseResume()` only accepts `AUDIO_LOCALFILE` or `ST_WEBSTREAM`.
HTTP files use `ST_WEBFILE`; without that case playback works but pause/resume
cannot be preserved. Apply the patch only to that matching implementation, then
compile and test the exact board. Other audio versions require API review.
The firmware only reports paused after the audio library accepts the operation.

The tested dependency also uses `audio-tts-compatibility.patch` to select the
working speech hostname and bound the saved speech text to its actual allocation.
The WROVER example prioritises PSRAM for general allocations above 256 bytes so
internal-only TLS allocations have enough memory for certificate parsing.
