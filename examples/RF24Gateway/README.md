# RF24Network master migration — prepared, not deployed

The packet decoder and native HA profile are prepared for the existing ESP32-S3
RF24Network gateway. This directory is not yet a flashable sketch.

The decoder preserves all nine packed sensor layouts and their octal addresses:
`01`, `02`, `03`, `04`, `05`, `033`, `034`, `011`, `013`. The switch destination
remains octal `012`. RF channel stays 90, 1 Mbps. There are 33 sensor channels and
one switch channel. The separate legacy `home/node4/relay/set` entity has no
handler in the supplied gateway sketch and is excluded; no imaginary command
support is added.

The native adapter preserves sample ages and starts measurements as unknown until
real packets arrive. It routes the new `bridge.rf24` profile without changing the
existing ESP-NOW bridge. Switch confirmation is explicitly a successful
`RF24Network.write`, not physical position feedback.

## Verification completed

- Production C++ decoder: exact packet sizes, all nine node layouts, unknown
  addresses, truncated/oversized packets, NaN/range rejection and 100,000 random
  malformed packets, with AddressSanitizer and UndefinedBehaviorSanitizer.
- 17 HA adapter tests in the actual HA Python 3.14 environment, including all
  existing ESP-NOW regression tests and five RF24 profile/command/freshness tests.
- Private backups of the existing local master sketches, HA configuration,
  MQTT entities and entity/device registries saved before preparation.

## Required before full firmware build / OTA deployment

The running gateway advertises password-protected OTA. The user-supplied sketch
sets an empty OTA password, and all located local source copies predate OTA/Telnet.
The current OTA password/newer source and the actual S3 flash/PSRAM/menu settings
must be resolved before claiming an exact-target firmware build or uploading.
A working matching rollback image has not yet been identified.

The complete sketch must retain OLED SPI wiring (DC21, CS47, reset20), RF24
CE4/CSN5, Telnet and OTA. GPIO20 is also native USB D+ on S3: USB CDC/menu choices
must respect the existing OLED wiring. The common SPI bus needs a single owner.
The pasted sketch's Wi-Fi/MQTT waits, unknown-packet loop, cross-core RF24 access
and 29-slot buffer for 34 display lines must be repaired. Do not send a switch
command automatically at boot or during verification.

No live firmware, HA entity migration, peer firmware or RF switch action has
been changed by this preparation. Credentials and private backups are excluded
from Git.
