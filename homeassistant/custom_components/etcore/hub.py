from __future__ import annotations

import asyncio
import base64
import hashlib
import hmac
import os
import json
import logging
import socket
import time
from typing import Any, Callable

from homeassistant.core import HomeAssistant

from .const import (
    ANNOUNCE_INTERVAL,
    CONF_PORT,
    CONF_PSK_HEX,
    DEFAULT_LEASE_TTL,
    LEASE_SWEEP_INTERVAL,
    MULTICAST_GROUP,
    PORT,
)

try:
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
except Exception:
    ChaCha20Poly1305 = None

_LOGGER = logging.getLogger(__name__)

# The command seq is saved ahead in blocks, so after a restart (or a crash) the hub
# continues above anything it may already have sent. Devices drop commands whose seq
# is not higher than the last one, so a seq that went backwards would lock them out.
CMD_SEQ_BLOCK = 1000

# Commands are resent if no ack arrives in time (spec: 300 ms, 2 retries).
ACK_TIMEOUT = 0.3
CMD_RETRIES = 2

# A misconfigured device repeats the same failure with every packet; log it once
# per device per window and report how many were held back.
WARN_WINDOW = 300.0

# A new device session is challenged at most this often (seconds), and has this
# long to answer.
CHALLENGE_INTERVAL = 1.0
CHALLENGE_TTL = 10


class _HubProtocol(asyncio.DatagramProtocol):
    def __init__(self, hub: "EtCoreHub") -> None:
        self._hub = hub

    def datagram_received(self, data: bytes, addr: tuple) -> None:
        self._hub._handle_datagram(data, addr)


class EtCoreHub:
    def __init__(
        self,
        hass: HomeAssistant,
        config: dict[str, Any] | None = None,
        counter_store: Any = None,
    ):
        self.hass = hass
        self._psk = self._parse_psk((config or {}).get(CONF_PSK_HEX))
        self._port = int((config or {}).get(CONF_PORT) or PORT)
        # Key for multicast announces. The label starts with a 0 byte, which no
        # device id can contain, so it can never equal a device key.
        self._site_key = hashlib.sha256(self._psk + b"\0etcore-announce").digest() if self._psk else None
        self._src_ip_cache: dict[str, tuple[str, float]] = {}
        self._hub_session = os.urandom(7)
        self._ch_secret = os.urandom(32)  # per hub run: old runs' challenges are void
        self._transport: asyncio.DatagramTransport | None = None
        self._tasks: list[asyncio.Task] = []
        self._devices: dict[str, dict[str, Any]] = {}
        self._message_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._status_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._events: list[dict[str, Any]] = []
        self._rx_count = 0
        self._decrypt_fail_count = 0
        self._replay_count = 0
        self._unproven_count = 0
        self._nohub_count = 0
        self._warned: dict[str, tuple[float, int]] = {}
        self._pending: dict[str, dict[str, Any]] = {}
        self._cmd_fail_count = 0
        self._seq = 0
        self._announce_seq = 0
        # Object with async_load() -> int and async_save(int); see __init__.py.
        self._counter_store = counter_store
        self._seq_reserved = 0

    @property
    def devices(self) -> dict[str, dict[str, Any]]:
        return dict(self._devices)

    def register_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._message_listeners.append(cb)

    def register_status_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._status_listeners.append(cb)

    async def async_start(self) -> None:
        if self._counter_store is not None:
            self._seq = int(await self._counter_store.async_load() or 0) & 0xFFFFFFFF
            self._seq_reserved = self._seq + CMD_SEQ_BLOCK
            await self._counter_store.async_save(self._seq_reserved)
        loop = asyncio.get_running_loop()
        # Event-loop UDP (no executor thread blocked in recvfrom).
        self._transport, _ = await loop.create_datagram_endpoint(
            lambda: _HubProtocol(self), sock=self._build_socket()
        )
        self._tasks.append(self.hass.loop.create_task(self._lease_sweeper()))
        self._tasks.append(self.hass.loop.create_task(self._announcer()))

    async def async_stop(self) -> None:
        for task in self._tasks:
            task.cancel()
        self._tasks.clear()
        for pend in self._pending.values():
            if pend["timer"]:
                pend["timer"].cancel()
        self._pending.clear()
        if self._transport:
            self._transport.close()
            self._transport = None

    def _build_socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except Exception:
            pass
        sock.bind(("", self._port))
        mreq = socket.inet_aton(MULTICAST_GROUP) + socket.inet_aton("0.0.0.0")
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        sock.setblocking(False)
        _LOGGER.info("ET-Core hub listening UDP *:%s mcast=%s", self._port, MULTICAST_GROUP)
        return sock

    def _send_raw(self, data: bytes, addr: tuple) -> None:
        if self._transport:
            self._transport.sendto(data, addr)

    def _handle_datagram(self, data: bytes, addr: tuple) -> None:
        try:
            msg = json.loads(data.decode("utf-8"))
        except Exception:
            return

        if not isinstance(msg, dict) or msg.get("v") != 1:
            return
        if msg.get("type") == "announce":
            return  # our own multicast coming back, or another hub

        msg["_src_ip"] = addr[0]
        if not self._decode_payload(msg):
            return  # counted inside: decrypt failure, replay or unproven session
        self._rx_count += 1
        self._record_message(msg, addr[0])
        self._remember_event(msg)

        for cb in list(self._message_listeners):
            self.hass.add_job(cb, msg)

    def _parse_psk(self, value: Any) -> bytes | None:
        psk = str(value or "").strip()
        if not psk:
            return None
        try:
            raw = bytes.fromhex(psk)
        except ValueError:
            _LOGGER.warning("ET-Core PSK is not valid hex; encrypted packets disabled")
            return None
        if len(raw) != 32:
            _LOGGER.warning("ET-Core PSK must be 32 bytes / 64 hex chars; encrypted packets disabled")
            return None
        return raw

    @staticmethod
    def _aad(mtype: str, dev_id: str, seq: Any) -> bytes:
        """Envelope fields bound into the encryption (must match ETCore::_aad)."""
        try:
            seq_n = int(seq)
        except (TypeError, ValueError):
            seq_n = -1
        return f"v1|{mtype}|{dev_id}|{seq_n}".encode("utf-8")

    def _warn_limited(self, key: str, msg: str, *args: Any) -> None:
        now = time.monotonic()
        last, held = self._warned.get(key, (0.0, 0))
        if last and now - last < WARN_WINDOW:
            self._warned[key] = (last, held + 1)
            return
        self._warned[key] = (now, 0)
        if held:
            msg += " (%d more like this since the last warning)"
            args = (*args, held)
        _LOGGER.warning(msg, *args)

    def _key_for(self, dev_id: str) -> bytes | None:
        if not self._psk:
            return None
        return hashlib.sha256(self._psk + dev_id.encode("utf-8")).digest()

    def _decode_payload(self, msg: dict[str, Any]) -> bool:
        payload = msg.get("payload")
        if not isinstance(payload, dict) or payload.get("_enc") != 1:
            # With a PSK set every device packet must be encrypted (hello too, so a
            # forged one cannot rename a device); a plaintext one could come from
            # anyone, or from a device that has no key.
            if self._psk:
                dev_id = str(msg.get("id") or "")
                if dev_id and dev_id != "hub":
                    self._warn_limited(f"plain:{dev_id}",
                                       "ET-Core %s sent %s without encryption (no key on the device?); ignored",
                                       dev_id, msg.get("type"))
                self._decrypt_fail_count += 1
                return False
            return True
        dev_id = str(msg.get("id") or "")
        key = self._key_for(dev_id)
        if not key or ChaCha20Poly1305 is None:
            self._warn_limited(f"nokey:{dev_id}", "ET-Core encrypted payload from %s ignored: crypto not configured", dev_id)
            self._decrypt_fail_count += 1
            return False
        try:
            nonce = base64.b64decode(str(payload.get("n") or ""), validate=True)
            ct = base64.b64decode(str(payload.get("ct") or ""), validate=True)
            tag = base64.b64decode(str(payload.get("tag") or ""), validate=True)
            if len(nonce) != 12 or len(tag) != 16 or nonce[0] != 1:
                raise ValueError("bad nonce or tag")
            aad = self._aad(str(msg.get("type") or ""), dev_id, msg.get("seq"))
            plain = ChaCha20Poly1305(key).decrypt(nonce, ct + tag, aad)
            payload = json.loads(plain.decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("payload is not an object")
            # Replay check: nonce bytes 1-7 are the device's per-boot session and
            # seq (authenticated via AAD) must keep rising within a session.
            session = nonce[1:8].hex()
            seq = int(msg.get("seq") or 0)
            dev = self._devices.setdefault(dev_id, {"id": dev_id})
            if dev.get("rx_session") == session:
                if seq <= int(dev.get("rx_seq") or 0):
                    self._replay_count += 1
                    return False
            elif not self._session_proven(dev, session, payload, msg.get("_src_ip")):
                return False
            dev["rx_session"] = session
            dev["rx_seq"] = seq
            msg["payload"] = payload
            msg["_encrypted"] = True
            return True
        except Exception as exc:
            self._decrypt_fail_count += 1
            self._warn_limited(f"dec:{dev_id}", "ET-Core decrypt failed for %s (wrong key?): %s", dev_id, exc.__class__.__name__)
            return False

    def _session_proven(self, dev: dict[str, Any], session: str, payload: dict[str, Any], ip: Any) -> bool:
        """A session this hub has not seen yet must prove it is live, not recorded.

        Packets recorded during an earlier boot decrypt fine and carry another
        session, so on their own they could keep a dead device "online". The hub
        sends the new session a challenge (unicast announce, bound to the session
        by _ds); only a packet that carries it back starts the session.

        The challenge is computed, not stored: time + HMAC(per-run secret, device,
        session, time), valid for CHALLENGE_TTL; a session that was replaced is
        retired for good. So recordings of many old boots
        cannot push the real one out of a table, and each session has its own
        rate limit, so a flood of them cannot use up the real session's turn.
        """
        # A session that was live and then replaced (the device rebooted) never
        # starts again: its own recorded packets may still carry a valid challenge.
        retired: list[str] = dev.setdefault("retired", [])
        if session not in retired and self._check_challenge(dev["id"], session, payload.get("ch")):
            dev.pop("challenged", None)
            if dev.get("rx_session"):
                retired.append(dev["rx_session"])
                del retired[:-8]
            return True
        self._unproven_count += 1
        now = time.monotonic()
        sent: dict[str, float] = dev.setdefault("challenged", {})
        if ip and now - sent.get(session, -CHALLENGE_INTERVAL) >= CHALLENGE_INTERVAL:
            sent.pop(session, None)
            sent[session] = now
            while len(sent) > 16:  # forgetting one only means it may be asked again sooner
                sent.pop(next(iter(sent)))
            dev["told_hub_at"] = time.time()
            self._send_announce(str(ip), hello=not dev.get("name"), dev_id=dev["id"],
                                ds=session, ch=self._make_challenge(dev["id"], session, int(now)))
        return False

    def _make_challenge(self, dev_id: str, session: str, t: int) -> str:
        t &= 0xFFFFFFFF
        mac = hmac.new(self._ch_secret, f"{dev_id}|{session}|{t}".encode(), hashlib.sha256).hexdigest()[:16]
        return f"{t:08x}{mac}"

    def _check_challenge(self, dev_id: str, session: str, ch: Any) -> bool:
        if not isinstance(ch, str) or len(ch) != 24:
            return False
        try:
            t = int(ch[:8], 16)
        except ValueError:
            return False
        if not 0 <= int(time.monotonic()) - t <= CHALLENGE_TTL:
            return False
        return hmac.compare_digest(ch, self._make_challenge(dev_id, session, t))

    def _encode_payload(self, dev_id: str, seq: int, payload: dict[str, Any]) -> dict[str, Any]:
        key = self._key_for(dev_id)
        if not key or ChaCha20Poly1305 is None:
            return payload

        # Address the command to the device's current boot: a recorded command
        # replayed after the device rebooted no longer matches and is dropped.
        payload = dict(payload, _ds=str((self._devices.get(dev_id) or {}).get("rx_session") or ""))

        nonce = bytearray(12)
        nonce[0] = 2
        nonce[1:8] = self._hub_session
        nonce[8] = seq & 0xFF
        nonce[9] = (seq >> 8) & 0xFF
        nonce[10] = (seq >> 16) & 0xFF
        nonce[11] = (seq >> 24) & 0xFF
        plain = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        out = ChaCha20Poly1305(key).encrypt(bytes(nonce), plain, self._aad("cmd", dev_id, seq))
        return {
            "_enc": 1,
            "alg": "C20P1305",
            "n": base64.b64encode(bytes(nonce)).decode("ascii"),
            "ct": base64.b64encode(out[:-16]).decode("ascii"),
            "tag": base64.b64encode(out[-16:]).decode("ascii"),
        }

    def _record_message(self, msg: dict[str, Any], ip: str) -> None:
        dev_id = str(msg.get("id") or "")
        if not dev_id or dev_id == "hub":
            return

        mtype = str(msg.get("type") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        now = time.time()

        dev = self._devices.setdefault(dev_id, {"id": dev_id})
        was_online = bool(dev.get("online", False))

        # Tell a device where the hub is when it says hello (it just booted) or the
        # first time this hub run hears it, so it can switch to unicast without
        # waiting for the next periodic announce. If we do not know its name yet,
        # ask for a hello in the same unicast and keep asking every 10 s: multicast
        # (the start-up announce) is often lost on WiFi.
        told = float(dev.get("told_hub_at") or 0)
        want_hello = mtype != "hello" and not dev.get("name")
        lost = bool(payload.get("nohub")) and now - told >= 1.0  # device lost track of us
        if lost:
            self._nohub_count += 1
        if (mtype == "hello" and now - told >= 1.0) or not told or lost or (want_hello and now - told >= 10.0):
            dev["told_hub_at"] = now
            self._send_announce(ip, hello=want_hello, dev_id=dev_id)
        # With a PSK only authenticated packets get this far.
        dev["class"] = msg.get("class") or dev.get("class")
        dev["boot"] = msg.get("boot") or dev.get("boot")
        dev["last_addr"] = ip
        dev["last_seen"] = now

        if mtype == "hello":
            dev["name"] = payload.get("name", dev_id)
            dev["fw"] = payload.get("fw")
            dev["features"] = payload.get("features", [])
            effects = payload.get("effects")
            dev["effects"] = [str(e) for e in effects] if isinstance(effects, list) else []
            # The whole hello (minus the challenge) for the platforms: switch
            # lists, fan presets, sensor metadata, model, ...
            dev["hello"] = {k: v for k, v in payload.items() if k != "ch"}

        if mtype == "ack":
            self._handle_ack(dev_id, payload)

        if mtype == "lease":
            ttl = int(payload.get("ttl") or DEFAULT_LEASE_TTL)
            dev["lease_ttl"] = max(2, ttl)
            dev["lease_expires"] = now + dev["lease_ttl"]
            dev["rssi"] = payload.get("rssi")
            dev["uptime"] = payload.get("uptime")

        if mtype in ("lease", "state", "hello"):
            dev["online"] = True
            if not was_online:
                self._fire_status(dev_id, "online")

        # The device reports a problem (sendError): log it, rate-limited, and fire
        # an event automations can use.
        if mtype == "error":
            code = str(payload.get("code") or "error")
            message = str(payload.get("message") or "")
            self._warn_limited(f"err:{dev_id}:{code}", "ET-Core device %s reports %s: %s", dev_id, code, message)
            self.hass.bus.async_fire("etcore_device_error", {"id": dev_id, "code": code, "message": message})

        # A planned restart (OTA, reboot): offline now, not when the lease runs out.
        # Only an authenticated bye of the current session with a rising seq gets here.
        if mtype == "bye":
            dev["lease_expires"] = now
            if was_online:
                dev["online"] = False
                self._fire_status(dev_id, "bye")

    async def _announcer(self) -> None:
        # First announce asks every device for a hello (names after an HA restart).
        self._send_announce(MULTICAST_GROUP, hello=True)
        while True:
            await asyncio.sleep(ANNOUNCE_INTERVAL)
            self._send_announce(MULTICAST_GROUP, hello=False)

    def _source_ip(self, target: str) -> str:
        """The address our packets to target leave from (what the device sees)."""
        now = time.monotonic()
        hit = self._src_ip_cache.get(target)
        if hit and now - hit[1] < 60:
            return hit[0]
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect((target, self._port))  # no packet is sent; the kernel picks the route
            ip = s.getsockname()[0]
        finally:
            s.close()
        self._src_ip_cache[target] = (ip, now)
        return ip

    def _send_announce(self, target: str, *, hello: bool, dev_id: str | None = None,
                       ds: str | None = None, ch: str | None = None) -> None:
        """Multicast (dev_id None, site key) or unicast to one device (its key).

        With a PSK the payload is encrypted and carries the hub's own address, so
        a device only follows an announce that really came from this hub; a
        unicast one also carries _ds, the device's current session.
        """
        if not self._transport:
            return
        self._announce_seq = (self._announce_seq + 1) & 0xFFFFFFFF
        seq = self._announce_seq
        to = dev_id or "hub"
        try:
            payload: dict[str, Any] = {"hello": hello, "hub": self._source_ip(target)}
            key = self._key_for(dev_id) if dev_id else self._site_key
            if key and ChaCha20Poly1305 is not None:
                if dev_id:
                    payload["_ds"] = ds or str((self._devices.get(dev_id) or {}).get("rx_session") or "")
                if ch:
                    payload["ch"] = ch
                nonce = bytes([3]) + self._hub_session + seq.to_bytes(4, "little")
                out = ChaCha20Poly1305(key).encrypt(
                    nonce, json.dumps(payload, separators=(",", ":")).encode("utf-8"),
                    self._aad("announce", to, seq))
                payload = {
                    "_enc": 1,
                    "alg": "C20P1305",
                    "n": base64.b64encode(nonce).decode("ascii"),
                    "ct": base64.b64encode(out[:-16]).decode("ascii"),
                    "tag": base64.b64encode(out[-16:]).decode("ascii"),
                }
            msg = {"v": 1, "type": "announce", "id": to, "class": "hub", "seq": seq, "payload": payload}
            self._send_raw(json.dumps(msg, separators=(",", ":")).encode("utf-8"), (target, self._port))
        except OSError as exc:
            _LOGGER.debug("ET-Core announce to %s failed: %s", target, exc)

    async def _lease_sweeper(self) -> None:
        while True:
            await asyncio.sleep(LEASE_SWEEP_INTERVAL)
            now = time.time()
            for dev_id, dev in list(self._devices.items()):
                if not bool(dev.get("online", False)):
                    continue
                expires = float(dev.get("lease_expires") or (dev.get("last_seen", 0) + DEFAULT_LEASE_TTL))
                if now > expires:
                    dev["online"] = False
                    self._fire_status(dev_id, "offline")

    def _fire_status(self, dev_id: str, reason: str) -> None:
        dev = self._devices.get(dev_id) or {}
        status = {
            "id": dev_id,
            "online": bool(dev.get("online", False)),
            "reason": reason,
            "last_seen": float(dev.get("last_seen") or 0),
            "last_addr": dev.get("last_addr"),
        }
        self.hass.bus.async_fire("etcore_device_status", status)
        self._events.append({
            "ts": time.time(),
            "type": "status",
            "id": dev_id,
            "status": "online" if status["online"] else "offline",
            "reason": reason,
        })
        self._events = self._events[-80:]
        for cb in list(self._status_listeners):
            self.hass.add_job(cb, status)

    def _remember_event(self, msg: dict[str, Any]) -> None:
        self._events.append({
            "ts": time.time(),
            "type": str(msg.get("type") or ""),
            "id": str(msg.get("id") or ""),
            "class": str(msg.get("class") or ""),
            "encrypted": bool(msg.get("_encrypted", False)),
            "src": msg.get("_src_ip"),
        })
        self._events = self._events[-80:]

    def get_diag_snapshot(self) -> dict[str, Any]:
        devices = []
        now = time.time()
        for dev in self._devices.values():
            item = dict(dev)
            item["age"] = round(now - float(dev.get("last_seen") or now), 3)
            item["expires_in"] = round(float(dev.get("lease_expires") or now) - now, 3)
            devices.append(item)
        devices.sort(key=lambda d: str(d.get("id") or ""))
        return {
            "ok": True,
            "encrypted": bool(self._psk),
            "rx_count": self._rx_count,
            "decrypt_failures": self._decrypt_fail_count,
            "replays_dropped": self._replay_count,
            "unproven_dropped": self._unproven_count,
            "hub_searches_answered": self._nohub_count,
            "commands_failed": self._cmd_fail_count,
            "commands_pending": len(self._pending),
            "devices": devices,
            "events": list(reversed(self._events[-50:])),
        }

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        if self._seq == 0:
            self._seq = 1
        # Save the next block when this one is half used, so the (async) save is
        # done long before the seq could reach the saved value.
        if self._counter_store is not None and self._seq >= self._seq_reserved - CMD_SEQ_BLOCK // 2:
            self._seq_reserved = self._seq + CMD_SEQ_BLOCK
            self.hass.loop.create_task(self._counter_store.async_save(self._seq_reserved))
        return self._seq

    def send_command(self, dev_id: str, dev_class: str, payload: dict[str, Any]) -> None:
        """Send a command and keep resending until the device acks it.

        The hub assigns cmd_id. A resend uses a new seq (devices drop old ones) but
        the same cmd_id, which the device recognises and only acks again.
        """
        if not self._transport:
            return
        if not (self._devices.get(dev_id) or {}).get("last_addr"):
            return
        cmd_id = str(self._next_seq())
        self._pending[cmd_id] = {
            "dev_id": dev_id,
            "class": dev_class,
            "payload": dict(payload, cmd_id=cmd_id),
            "tries": 0,
            "timer": None,
        }
        self._transmit(cmd_id, first=True)

    def _transmit(self, cmd_id: str, first: bool = False) -> None:
        pend = self._pending.get(cmd_id)
        if not pend or not self._transport:
            return
        dev_id = pend["dev_id"]
        ip = (self._devices.get(dev_id) or {}).get("last_addr")
        seq = int(cmd_id) if first else self._next_seq()
        msg = {
            "v": 1,
            "type": "cmd",
            "id": dev_id,
            "class": pend["class"],
            "seq": seq,
            "payload": self._encode_payload(dev_id, seq, pend["payload"]),
        }
        if ip:
            self._send_raw(json.dumps(msg, separators=(",", ":")).encode("utf-8"), (ip, self._port))
        pend["tries"] += 1
        pend["timer"] = self.hass.loop.call_later(ACK_TIMEOUT, self._ack_timeout, cmd_id)

    def _ack_timeout(self, cmd_id: str) -> None:
        pend = self._pending.get(cmd_id)
        if not pend:
            return
        if pend["tries"] <= CMD_RETRIES:
            self._transmit(cmd_id)
            return
        self._pending.pop(cmd_id, None)
        self._cmd_fail_count += 1
        self.hass.bus.async_fire("etcore_command_failed", {
            "id": pend["dev_id"], "class": pend["class"], "cmd_id": cmd_id,
            "payload": {k: v for k, v in pend["payload"].items() if k != "cmd_id"},
        })
        self._warn_limited(f"cmdfail:{pend['dev_id']}",
                           "ET-Core command to %s not acknowledged after %d tries",
                           pend["dev_id"], pend["tries"])

    def _handle_ack(self, dev_id: str, payload: dict[str, Any]) -> None:
        cmd_id = str(payload.get("cmd_id") or "")
        pend = self._pending.get(cmd_id)
        if not pend or pend["dev_id"] != dev_id:
            return
        if pend["timer"]:
            pend["timer"].cancel()
        if payload.get("ok") is False and payload.get("err") == "stale":
            # Our seq is behind what the device already accepted (HA restored from
            # a backup). The reply is authenticated, so jump above it and resend.
            last = int(payload.get("last_seq") or 0) & 0xFFFFFFFF
            if last >= self._seq:
                _LOGGER.info("ET-Core: %s reports seq %s ahead of the hub (%s); moving up",
                             dev_id, last, self._seq)
                self._seq = last
                self._seq_reserved = 0  # forces a save of a new block on the next seq
            if pend["tries"] <= CMD_RETRIES:
                self._transmit(cmd_id)
            else:
                pend["timer"] = self.hass.loop.call_later(0, self._ack_timeout, cmd_id)
            return
        self._pending.pop(cmd_id, None)
