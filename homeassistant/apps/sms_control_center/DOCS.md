# SMS Control Center 2.1

Private Home Assistant Ingress inbox, outbox and one-SMS composer. The app now
observes native HA SMS events and the existing outgoing sensor over an authenticated
HA WebSocket; it does not connect to MQTT. Sending still calls only
`sms_control_center.send_sms` with the configured default recipient.

Install the accompanying SMS Control Center HA component, select ET-Core in its
Configure options, and use the encrypted SIM7600 gateway example. The app retains
its existing `/data/sms.sqlite3` database, message history, composer request IDs,
duplicate guards, export, search, phone layout and Ingress restrictions. Upgrading
creates new reference/result tables without removing old messages.

`Modem accepted` is modem submission success, not a handset delivery receipt.
`Submission uncertain` must not be automatically retried. Incoming events are
deduplicated in the history database. Outgoing state is reconciled after reconnect;
messages arriving while this app's event connection is down can still be missing
from the app history, even when HA processed them.

Install this directory as a local Home Assistant app. No passwords or phone numbers
are stored in its source. The gateway private settings and compiled binaries must
stay out of Git.
