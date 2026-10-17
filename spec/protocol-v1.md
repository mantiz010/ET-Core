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

## Security Rules

ET-Core crypto must not reuse an AEAD nonce for the same key.

Encrypted mode:

- Device key: `sha256(master_psk || device_id)`.
- Payload wrapper: `{ "_enc": 1, "alg": "C20P1305", "n": "...", "ct": "...", "tag": "..." }`.
- Nonce is 12 bytes and base64 encoded in `n`.
- `nonce[0]` is the direction: `1` device to hub, `2` hub to device.
- `nonce[1..4]` is fresh random session data generated at boot/start.
- `nonce[8..11]` is the sender's monotonic sequence.
- Command counters are monotonic per hub boot/session.
- State counters are monotonic per device boot.
- With a PSK configured, the hub accepts `lease`, `state` and `ack` only encrypted;
  plaintext ones are dropped. `hello` stays plaintext and only updates name and
  features: it never marks a device online.
