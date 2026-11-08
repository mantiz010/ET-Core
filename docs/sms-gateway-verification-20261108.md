# SMS gateway migration verification — 8 November 2026

The classic ESP32 SIM7600 gateway was OTA-updated to
`sim7600-etcore-2026.11.08.1`. Authenticated ET-Core discovery, leases, modem
readiness and persistent storage health were observed on the physical gateway.
No MQTT client is present in the new firmware.

A test SMS was accepted by the modem, arrived on the owner's handset, and the
owner's real reply was received over ET-Core. HA recognised the sender as trusted.
The SMS app showed the outgoing modem result and the received reply. The original
eight HA sensor/button entity IDs were unchanged, and every unexpired existing
app history record remained in SQLite. Both recipients' original settings were
preserved; no test text was sent to the secondary recipient.

The migration converted 23 direct SMS send actions and 17 incoming MQTT triggers
in this installation. Unrelated automation objects and scripts were unchanged.
Home Assistant configuration validation passed. Existing IDs/templates/trusted
sender rules were retained; these mappings are installation-specific.

Verification: 12 HA transport tests, 17 app tests, three YAML migration tests,
production C++ validation with AddressSanitizer/UndefinedBehaviorSanitizer, and a
clean exact-target Arduino-ESP32 3.3.11 build with unchanged source. Application
size was 1,009,875 bytes; static RAM was 73,808 bytes. Libraries are listed in the
SMSGateway example README. HA tests ran in the actual HA Python 3.14 environment.

Private backups contain the original MQTT sketch, private settings, rollback
application image, HA configuration/registries, app source/data and a consistent
SQLite history backup. Credentials, real phone numbers, local configuration and
compiled binaries are excluded from Git.

The physical test does not establish unlimited throughput, lossless handling of
all modem URCs, unlimited duplicate protection, or handset delivery of every
future message. See the example's explicit queue/retry/delivery contract.
