# ET-Core Protocol v1

## Transport

ET-Core uses UDP on a local LAN.

Default multicast group: `239.10.0.2`  
Default port: `5560`

Devices multicast `hello` and `lease` until a hub address is learned. After learning the hub, devices may unicast `lease` and `state` to the hub for reliability.

## Envelope

Every message is JSON with this envelope:

```json
{
  "v": 1,
  "type": "lease",
  "id": "relay4",
  "class": "switch.relay4",
  "boot": "8f12a0c4d91b3321",
  "seq": 42,
  "payload": {}
}
```

Fields:

- `v`: protocol version.
- `type`: `hello`, `lease`, `state`, `cmd`, `ack`, or `bye`.
- `id`: stable device id.
- `class`: device class.
- `boot`: random boot id.
- `seq`: monotonic packet sequence for this boot.
- `payload`: message payload.

## Message Types

### hello

Sent after boot and when the hub asks for discovery.

```json
{
  "v": 1,
  "type": "hello",
  "id": "relay4",
  "class": "switch.relay4",
  "boot": "8f12a0c4d91b3321",
  "seq": 1,
  "payload": {
    "name": "Relay Board 4",
    "fw": "relay4-1.0.0",
    "lib": "ETCore-0.1.0",
    "features": ["lease", "ack", "state_on_change"],
    "channels": 4
  }
}
```

### lease

Small online proof. This replaces ping/pong.

```json
{
  "v": 1,
  "type": "lease",
  "id": "relay4",
  "class": "switch.relay4",
  "boot": "8f12a0c4d91b3321",
  "seq": 12,
  "payload": {
    "ttl": 6,
    "uptime": 120,
    "rssi": -62,
    "state_hash": "7c31a2b1"
  }
}
```

The hub marks the device unavailable when `now > last_lease + ttl`.

### state

Sent on state change and slow periodic refresh.

```json
{
  "v": 1,
  "type": "state",
  "id": "relay4",
  "class": "switch.relay4",
  "boot": "8f12a0c4d91b3321",
  "seq": 20,
  "payload": {
    "switches": {
      "1": true,
      "2": false,
      "3": false,
      "4": true
    }
  }
}
```

### cmd

Home Assistant to device.

```json
{
  "v": 1,
  "type": "cmd",
  "id": "relay4",
  "class": "switch.relay4",
  "seq": 100,
  "payload": {
    "cmd_id": "ha-1710000000-1",
    "switch_id": "1",
    "on": true
  }
}
```

### ack

Device confirms command.

```json
{
  "v": 1,
  "type": "ack",
  "id": "relay4",
  "class": "switch.relay4",
  "boot": "8f12a0c4d91b3321",
  "seq": 101,
  "payload": {
    "cmd_id": "ha-1710000000-1",
    "ok": true
  }
}
```

### announce

Hub to devices. Sent multicast when the hub starts (`"hello": true`) and then every
30 s (`"hello": false`), and unicast to a device when it says hello or is first
heard by this hub run. Devices learn the hub address from it and send leases,
state and acks unicast from then on. Devices never answer a periodic announce; an
announce with `"hello": true` gets one `hello` back after a random 0-2 s delay.
Multicast is often lost on WiFi (seen on the hardware test: an ESP32-C3 received no
multicast announce at all), so the hub also asks by unicast: the unicast announce it
sends on first contact carries `"hello": true` while it does not know the device's
name, repeated at most every 10 s until a hello arrives.
After answering an announce with `"hello": true` the library calls the sketch's
`onSync` handler, which should send the current `state`, so a restarted hub shows
the real state at once instead of a default until the next change or refresh.

```json
{"v": 1, "type": "announce", "id": "hub", "class": "hub", "seq": 7, "payload": {"hello": false}}
```

Known limit: `announce` is not authenticated, so a forged one can point devices at
the wrong address (their leases stop reaching the hub: denial of service, not
control). Devices only learn the hub address from a `cmd` after it authenticated.

### Command delivery

- The hub assigns every command a unique `cmd_id` (any `cmd_id` from the caller is
  replaced).
- If no `ack` with that `cmd_id` arrives within 300 ms the hub resends, up to 2
  times. Each resend has a new `seq` and the same `cmd_id`.
- The device remembers its last 4 `cmd_id`s: a resend of one of them is acked again
  (with the original `ok`) but not executed again.
- The library sends `ack` (`ok: true`) after the sketch's command handler unless the
  sketch already called `sendAck(cmd_id, ...)` for that command (e.g. `ok: false`).
- After the last retry the hub fires `etcore_command_failed`
  (`id`, `class`, `cmd_id`, `payload`) and logs one warning.

## Security Rules

ET-Core crypto must not reuse an AEAD nonce for the same key.

Encrypted mode:

- Device key: `sha256(master_psk || device_id)`.
- Payload wrapper: `{ "_enc": 1, "alg": "C20P1305", "n": "...", "ct": "...", "tag": "..." }`.
- Nonce is 12 bytes and base64 encoded in `n`.
- `nonce[0]` is the direction: `1` device to hub, `2` hub to device.
- `nonce[1..7]` is fresh random session data generated at boot/start (56 bits).
- `nonce[8..11]` is the sender's monotonic sequence.
- Command counters are monotonic per hub boot/session.
- State counters are monotonic per device boot.
- With a PSK configured, the hub accepts `lease`, `state` and `ack` only encrypted;
  plaintext ones are dropped. `hello` stays plaintext and only updates name, fw and
  features (and gets a unicast announce back): it never marks a device online and
  never changes the device address, class or boot, and it is not passed to the
  entity platforms. Known limit: a forged hello can change the displayed name.
- Associated data (AAD) for every encrypted payload is the UTF-8 string
  `v1|<type>|<id>|<seq>` taken from the envelope (for `cmd`, `id` is the target
  device). A packet whose `type`, `id` or `seq` was changed fails authentication.
- Devices drop commands with `seq` 0 or `seq` not greater than the last accepted one.
- Hub replay check: for each device the hub keeps the session (nonce bytes 1-7) and
  the last `seq`; an encrypted packet with the same session and a `seq` that is not
  higher is dropped. Known limit: a packet recorded during an *earlier* boot of the
  device carries a different session and is not caught by this check.
- The hub's command `seq` never goes backwards in normal use: it is saved in Home
  Assistant storage ahead of use in blocks of 1000 and continues from the saved value
  after a restart.
- Recovery when it does go back (HA restored from a backup, storage lost): an
  encrypted device that gets a genuine command for its current boot (decrypts, `_ds`
  matches) with a `seq` it has already passed does not run it and answers with an
  encrypted `ack` `{"cmd_id", "ok": false, "err": "stale", "last_seq": N}`. The hub
  moves its seq above N, saves a new block and resends. A replayed command only
  produces the same reply.
- Encrypted commands carry `_ds`: the target device's current session (nonce bytes
  1-7 of its own packets, 14 lowercase hex chars). Devices drop commands whose `_ds`
  is not their own session, so commands recorded before a device reboot are dead.
  The hub learns the session from the device's first encrypted packet after boot.
