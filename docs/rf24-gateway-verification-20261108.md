# RF24 gateway migration verification — 8 November 2026

The ESP32-S3 RF24Network master was OTA-updated to
`rf24-etcore-2026.11.08.1` with 4 MB flash and PSRAM disabled. The blank OTA
password was confirmed with an authentication-only zero-size request before any
firmware upload. The prior claim that `auth_upload=yes` rules out a blank password
was corrected: recent ArduinoOTA hashes the empty string too.

The exact target build passed with unchanged source. Program size was 1,016,917
bytes and static RAM 74,048 bytes. The production build did not link PubSubClient.
A separate reconstructed MQTT recovery build also compiled successfully, with an
application size of 991,709 bytes. It is a recovery option, not an original flash
readback. Private images and credentials are excluded from Git.

Physical observations after OTA: authenticated encrypted hello/state packets,
radio READY, OLED initialization successful, 20 received RF packets and zero
rejected packets in the first Telnet status capture. Telnet also streamed real
sensor values. OLED pixels were not independently observed. Later data continued
arriving through the normal radio/OLED/network loop.

All 34 migrated entity IDs, displayed names and units were unchanged: 33 sensors
and the existing switch command entity. Seventeen sensors had valid states before
and after, with none of the previously available sensors lost. Sixteen sensors
were already offline. The separate Node 4 MQTT relay was not migrated or changed.
No switch command was issued during deployment verification; its new command
state is honestly unknown until an explicit successful command. Radio transmission
success is not proof of physical actuation.

Old temperature history was observed (19,855 records in the preceding day) and
36 new records were observed after cutover. Bresser, ESP-NOW master and the SMS
ET-Core gateway remained online after HA startup. Configuration validation passed.

Tests: 17 HA adapter tests (including existing ESP-NOW regression tests), all nine
RF node layouts, wrong lengths/addresses/invalid values, and 100,000 malformed
packets using the production decoder with ASan/UBSan. Exact S3 compile receipts,
source/image hashes, entity mappings and private backups remain in the local work
folder. Source and firmware settings are documented in the RF24Gateway example.
