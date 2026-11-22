# Native audio player (staged development)

Class: `media_player.audio`. This is native ET-Core control/state, with no MQTT
client or broker. Audio bytes continue to stream directly over HTTP/HTTPS.
The Home Assistant platform and WROVER example are staged for verification;
existing players must not be replaced before device and Music Assistant tests.

## Encrypted wire contract

All messages use ET-Core's existing authenticated session, replay protection,
command IDs, deduplication, acknowledgements and lease expiry. No second control
transport or plaintext fallback is introduced. Transport acknowledgement means
accepted/queued, never that audio was played. Only a subsequent device state
changes the displayed playback state.

A hello includes the normal identity/name/model/firmware plus:

```json
{"media_capabilities":["play_media","play","pause","stop","volume_set","power","tts"]}
```

Advertise only implemented capabilities. A valid capability-bearing hello must
precede entity creation. The stable unique ID is `etcore_<device-id>_media_player`.
A reboot clears the prior session's playback state until the device reports again.

Commands (`cmd_id` is added by the hub):

| Operation | Fields | Meaning |
|---|---|---|
| `play_media` | `url` | Start an HTTP/HTTPS stream |
| `play` | — | Resume paused audio, or restart the retained URL |
| `pause` | — | Pause currently running audio |
| `stop` | — | Stop audio and report idle |
| `volume` | `volume` in 0..1 | Set volume; HA steps use 0.05 |
| `power` | boolean `on` | Software on/off; off stops audio, not device power |
| `tts` | `text`, optional `language` | Direct device speech, default language `en` |

A complete application command is limited to 768 bytes of JSON (ASCII-escaped
encoding), with strings limited to 640 UTF-8 bytes. These limits reserve space
for the command ID and encrypted envelope within the device's 1500-byte receive
packet. Oversized commands fail explicitly; they are never truncated. URL user
credentials, non-HTTP schemes and control characters are rejected. This does not
restrict URLs to the public internet: local HA and Music Assistant audio servers
are intentional supported sources.

State fields: `state` (`idle`, `playing`, `paused`, `buffering`, `off`), normalized
`volume`, optional `position` and `duration` in seconds, `url`, `title`, `artist`,
`content_type`, and a bounded `error` code. Null clears explicitly unknown
metadata. Invalid values cannot become numeric state. Availability uses the
existing authenticated device lease, independent of whether a song is playing.

HA media-source and generated TTS URIs are resolved with Home Assistant's current
media-source helpers before a playable URL is sent. Explicit `media_type: tts`
retains the device-side speech path. Seek, grouping, queue ownership and audio
announcements are not advertised by this first endpoint; Music Assistant owns
its queue and supplies stream URLs as it did for the previous HA player.

## WROVER firmware and OTA

The example uses MAX98357A BCLK=26, LRC=25, DOUT=22 and requires working PSRAM.
Audio is owned by one FreeRTOS task. ET-Core callbacks only validate and queue
commands. The main loop services the encrypted transport and ArduinoOTA while
a stream connection or decoder operation is pending. No SD card is initialized.

Test target: `esp32:esp32:esp32wrover:PartitionScheme=min_spiffs,FlashMode=dio,FlashFreq=80,UploadSpeed=115200`.
The ESP32 core's `min_spiffs` layout has two 1,966,080-byte application slots.
The previous Huge APP layout has no OTA slot. The first migration must use USB,
with the actual chip/flash/PSRAM verified and its current flash backed up before
changing the partition table. Keep the original working firmware and settings.
Compilation for a saved 4 MB target is not measurement of a connected board.

Set a distinct OTA password of at least 12 characters. The firmware refuses to
start OTA with its placeholder password or without an update partition. The OTA
password is never included in device hello/state. OTA pauses audio and discards
queued media operations, sends a planned goodbye and reports update progress to
serial. ET-Core encryption does not make ArduinoOTA's transfer encrypted.
No firmware update, erase, reboot or hardware control is performed by tests.

## Migration acceptance gates

1. Preserve the working firmware, dependencies, HA configuration/entity registry,
   Music Assistant player settings and dashboards privately.
2. Verify exact-board build, OTA slot fit and clean dependency closure. Publish
   generic examples only; never include real devices, Wi-Fi credentials or keys.
3. Test authenticated transport, command retry/deduplication and lease expiry on
   isolated loopback ports. Test the HA platform against the installed HA API.
4. Confirm encrypted hardware discovery and state, then real stream/TTS playback,
   pause/resume/stop, volume and software power with the owner present.
5. Disable the old MQTT entity only after the new endpoint passes. Preserve
   `media_player.esp32_media_player` via a reviewed entity-ID handover. The Music
   Assistant wrapper is preserved or reconfigured explicitly; do not silently
   delete/re-add it, which could change dashboard/automation references.
6. Verify Music Assistant playback, queue progression and TTS end to end. Test a
   password-protected OTA update and recovery before declaring migration done.

Until all gates pass, this feature is not a completed hardware migration.
