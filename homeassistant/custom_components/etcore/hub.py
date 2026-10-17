from __future__ import annotations

import asyncio
import base64
import hashlib
import os
import json
import logging
import socket
import time
from typing import Any, Callable

from homeassistant.core import HomeAssistant

from .const import CONF_PSK_HEX, DEFAULT_LEASE_TTL, LEASE_SWEEP_INTERVAL, MULTICAST_GROUP, PORT

try:
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
except Exception:
    ChaCha20Poly1305 = None

_LOGGER = logging.getLogger(__name__)

# The command seq is saved ahead in blocks, so after a restart (or a crash) the hub
# continues above anything it may already have sent. Devices drop commands whose seq
# is not higher than the last one, so a seq that went backwards would lock them out.
CMD_SEQ_BLOCK = 1000


class EtCoreHub:
    def __init__(
        self,
        hass: HomeAssistant,
        config: dict[str, Any] | None = None,
        counter_store: Any = None,
    ):
        self.hass = hass
        self._psk = self._parse_psk((config or {}).get(CONF_PSK_HEX))
        self._hub_session = os.urandom(4)
        self._sock: socket.socket | None = None
        self._tasks: list[asyncio.Task] = []
        self._devices: dict[str, dict[str, Any]] = {}
        self._message_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._status_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._events: list[dict[str, Any]] = []
        self._rx_count = 0
        self._decrypt_fail_count = 0
        self._replay_count = 0
        self._seq = 0
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
        self._sock = self._build_socket()
        self._tasks.append(self.hass.loop.create_task(self._receiver(loop)))
        self._tasks.append(self.hass.loop.create_task(self._lease_sweeper()))

    async def async_stop(self) -> None:
        for task in self._tasks:
            task.cancel()
        self._tasks.clear()
        if self._sock:
            self._sock.close()
            self._sock = None

    def _build_socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except Exception:
            pass
        sock.bind(("", PORT))
        mreq = socket.inet_aton(MULTICAST_GROUP) + socket.inet_aton("0.0.0.0")
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        sock.setblocking(True)
        _LOGGER.info("ET-Core hub listening UDP *:%s mcast=%s", PORT, MULTICAST_GROUP)
        return sock

    async def _receiver(self, loop) -> None:
        if not self._sock:
            return

        while True:
            try:
                data, addr = await loop.run_in_executor(None, self._sock.recvfrom, 2048)
            except asyncio.CancelledError:
                return
            except OSError:
                await asyncio.sleep(1)
                continue

            try:
                msg = json.loads(data.decode("utf-8"))
            except Exception:
                continue

            if not isinstance(msg, dict) or msg.get("v") != 1:
                continue

            msg["_src_ip"] = addr[0]
            if not self._decode_payload(msg):
                self._decrypt_fail_count += 1
                continue
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

    def _key_for(self, dev_id: str) -> bytes | None:
        if not self._psk:
            return None
        return hashlib.sha256(self._psk + dev_id.encode("utf-8")).digest()

    def _decode_payload(self, msg: dict[str, Any]) -> bool:
        payload = msg.get("payload")
        if not isinstance(payload, dict) or payload.get("_enc") != 1:
            # With a PSK set, anything that proves a device is alive or reports its
            # state must be encrypted; a plaintext one could come from anyone.
            if self._psk and str(msg.get("type") or "") in ("lease", "state", "ack"):
                return False
            return True
        dev_id = str(msg.get("id") or "")
        key = self._key_for(dev_id)
        if not key or ChaCha20Poly1305 is None:
            _LOGGER.warning("ET-Core encrypted payload from %s ignored: crypto not configured", dev_id)
            return False
        try:
            nonce = base64.b64decode(str(payload.get("n") or ""), validate=True)
            ct = base64.b64decode(str(payload.get("ct") or ""), validate=True)
            tag = base64.b64decode(str(payload.get("tag") or ""), validate=True)
            if len(nonce) != 12 or len(tag) != 16 or nonce[0] != 1:
                return False
            aad = self._aad(str(msg.get("type") or ""), dev_id, msg.get("seq"))
            plain = ChaCha20Poly1305(key).decrypt(nonce, ct + tag, aad)
            # Replay check: nonce bytes 1-4 are the device's per-boot session and
            # seq (authenticated via AAD) must keep rising within a session.
            session = nonce[1:5].hex()
            seq = int(msg.get("seq") or 0)
            dev = self._devices.setdefault(dev_id, {"id": dev_id})
            if dev.get("rx_session") == session and seq <= int(dev.get("rx_seq") or 0):
                self._replay_count += 1
                return False
            dev["rx_session"] = session
            dev["rx_seq"] = seq
            msg["payload"] = json.loads(plain.decode("utf-8"))
            msg["_encrypted"] = True
            return isinstance(msg["payload"], dict)
        except Exception as exc:
            _LOGGER.warning("ET-Core decrypt failed for %s: %s", dev_id, exc.__class__.__name__)
            return False

    def _encode_payload(self, dev_id: str, seq: int, payload: dict[str, Any]) -> dict[str, Any]:
        key = self._key_for(dev_id)
        if not key or ChaCha20Poly1305 is None:
            return payload

        # Address the command to the device's current boot: a recorded command
        # replayed after the device rebooted no longer matches and is dropped.
        payload = dict(payload, _ds=str((self._devices.get(dev_id) or {}).get("rx_session") or ""))

        nonce = bytearray(12)
        nonce[0] = 2
        nonce[1:5] = self._hub_session
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
        dev["class"] = msg.get("class") or dev.get("class")
        dev["boot"] = msg.get("boot") or dev.get("boot")
        dev["last_addr"] = ip
        dev["last_seen"] = now

        if mtype == "hello":
            dev["name"] = payload.get("name", dev_id)
            dev["fw"] = payload.get("fw")
            dev["features"] = payload.get("features", [])

        if mtype == "lease":
            ttl = int(payload.get("ttl") or DEFAULT_LEASE_TTL)
            dev["lease_ttl"] = max(2, ttl)
            dev["lease_expires"] = now + dev["lease_ttl"]
            dev["rssi"] = payload.get("rssi")
            dev["uptime"] = payload.get("uptime")

        # hello is plaintext by design, so with a PSK it only updates the name.
        if mtype in ("lease", "state") or (mtype == "hello" and not self._psk):
            dev["online"] = True
            if not was_online:
                self._fire_status(dev_id, "online")

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
            "devices": devices,
            "events": list(reversed(self._events[-50:])),
        }

    def send_command(self, dev_id: str, dev_class: str, payload: dict[str, Any]) -> None:
        if not self._sock:
            return
        dev = self._devices.get(dev_id) or {}
        ip = dev.get("last_addr")
        if not ip:
            return
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        if self._seq == 0:
            self._seq = 1
        if self._counter_store is not None and self._seq >= self._seq_reserved:
            self._seq_reserved = self._seq + CMD_SEQ_BLOCK
            self.hass.loop.create_task(self._counter_store.async_save(self._seq_reserved))
        msg = {
            "v": 1,
            "type": "cmd",
            "id": dev_id,
            "class": dev_class,
            "seq": self._seq,
            "payload": self._encode_payload(dev_id, self._seq, payload),
        }
        self._sock.sendto(json.dumps(msg, separators=(",", ":")).encode("utf-8"), (ip, PORT))
