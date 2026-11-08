# SIM7600 SMS gateway over encrypted ET-Core

Classic ESP32 Dev Module + SIM7600SA-H. Replaces the gateway's MQTT client with
ChaCha20-Poly1305 authenticated ET-Core on UDP 5560. Keeps both default recipients,
optional explicit destination, incoming SMS, the existing modem initialization and
registration/GPRS recovery, and password-protected Arduino OTA.

## Wiring and target

| Connection | Existing ESP32 GPIO |
|---|---:|
| ESP32 TX → modem RX | 16 |
| ESP32 RX ← modem TX | 17 |
| Modem PWRKEY | 4 |
| Modem reset (defined by legacy sketch; not driven) | 5 |

UART1 is 115200 baud, 8N1. Wiring is inherited from the working gateway; use your
board's existing level shifting and modem power supply. This does not flash modem
firmware. Wi-Fi SSID, modem settings and recipient numbers are not interchangeable
with a different installation.

Verified target: `esp32:esp32:esp32`, 4 MB flash, default OTA partition, no PSRAM,
240 MHz, QIO 80 MHz, Arduino-ESP32 3.3.11. Libraries: ETCore 0.4.3,
ETChaCha20Poly1305 1.0.0, ArduinoJson 7.4.3, TinyGSM 0.12.0.

1. Copy `gateway_secrets.h.example` to `gateway_secrets.h` beside the sketch.
2. Enter Wi-Fi, both recipients, OTA password and the hub's 64-character hex key.
3. Use the supplied `sms_control_center` Home Assistant component. Select
   **ET-Core** in its Configure options after the gateway announces itself.
4. Initial installation requires USB unless Arduino OTA already runs. Future
   OTA uses `sim7600-sms-gateway.local:3232`, with your configured password.

Never commit your private header, binaries (which contain credentials), or local
backups. The example's `.gitignore` excludes those files.

## Home Assistant compatibility

Existing SMS Control Center entity IDs and `sms_control_center.send_sms` remain.
The optional `topic` argument is a compatibility **recipient selector**, not MQTT:
`home/sms/send` means primary, `home/sms/send2` means secondary. `number` overrides
the default destination. Unknown routes fail closed.

Incoming texts fire `sms_control_center_incoming` with `sender`, `message` and
`event_id`. Keep existing trusted-sender conditions. Change legacy SMS MQTT
triggers to this event and replace `trigger.payload_json` with
`trigger.event.data` in those automations only. `tools/migrate_sms_yaml.py` writes
converted copies into a separate directory; review before installing. It leaves
unrelated MQTT automations alone and refuses unsupported filtered MQTT triggers.

Molly and Device Doctor already call the SMS Control Center service, so their
sending entry point is unchanged. Legacy MQTT remains an optional HA backend for
rollback; the new gateway firmware contains no MQTT client.

## Request and delivery semantics

- Requests carry UUIDs. A command ACK means validation/queue acceptance, not a
  carrier or handset delivery receipt.
- HA waits for `queued` or `modem_accepted`. It raises on unknown acceptance and
  does **not** automatically resubmit the text.
- `modem_accepted` means TinyGSM received the modem's success response.
  `submission_unknown` means submission may have happened; retrying can duplicate
  the SMS. Neither result proves delivery to the handset.
- The last outgoing sensor exposes `request_id`, `transport` and
  `submission_status`. Result events use `sms_control_center_result`.
- Up to four requests queue in RAM. The last 32 accepted request UUIDs are reserved
  in ESP32 NVS **before** queueing; a reboot does not submit them again. Interrupted
  requests are not automatically resumed. HA uses a new UUID per deliberate send
  and never retries old requests automatically. This is bounded duplicate
  protection, not an unlimited exactly-once guarantee.
- Up to 16 received/result events persist in NVS and repeat until HA acknowledges
  them. HA persistently deduplicates its last 2048 events before firing automations.
  A crash between storing an incoming ID and firing its event can lose that action;
  automatic replay of a garage/door command is deliberately avoided.
- Queues and storage are bounded. Extended outages can fill them. Overflow/storage
  failures are logged; do not treat this as a replacement for carrier message
  storage. Legacy direct `+CMT` parsing and TinyGSM AT handling are retained;
  simultaneous unsolicited traffic during blocking modem commands is not proven
  lossless by this migration.
- ET-Core and OTA run on a separate task so legacy modem timeout/recovery waits do
  not block the encrypted network link. OTA waits for an active SMS send to finish.

## Rollback and verification

Before deployment save the original sketch, private header, last working OTA
application binary, HA component, entity/config registries and automations. A
rollback binary contains secrets and belongs in a private local backup, not Git.
Restore the legacy firmware, choose MQTT in SMS Control Center options, and restore
legacy SMS automation triggers/actions together.

The transport has HA tests for validation, duplicate events, disk failures,
offline/stale readiness, ordering and recipient selection. Firmware validation is
also exercised using the production `SmsValidation.h` with ASan/UBSan. A clean
exact-target compile proves buildability; actual sending/receiving and carrier
coverage must be checked on the physical installation.
