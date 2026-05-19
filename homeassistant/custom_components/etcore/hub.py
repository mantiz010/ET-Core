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


class EtCoreHub:
    def __init__(self, hass: HomeAssistant, config: dict[str, Any] | None = None):
        self.hass = hass
        self._psk = self._parse_psk((config or {}).get(CONF_PSK_HEX))
        self._hub_session = os.urandom(4)
        self._sock: socket.socket | None = None
        self._tasks: list[asyncio.Task] = []
        self._devices: dict[str, dict[str, Any]] = {}
        self._message_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._status_listeners: list[Callable[[dict[str, Any]], None]] = []
        self._seq = 0

    @property
    def devices(self) -> dict[str, dict[str, Any]]:
        return dict(self._devices)

    def register_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._message_listeners.append(cb)

    def register_status_listener(self, cb: Callable[[dict[str, Any]], None]) -> None:
        self._status_listeners.append(cb)

    async def async_start(self) -> None:
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
                continue
            self._record_message(msg, addr[0])

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

    def _key_for(self, dev_id: str) -> bytes | None:
        if not self._psk:
            return None
        return hashlib.sha256(self._psk + dev_id.encode("utf-8")).digest()

    def _decode_payload(self, msg: dict[str, Any]) -> bool:
        payload = msg.get("payload")
        if not isinstance(payload, dict) or payload.get("_enc") != 1:
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
            plain = ChaCha20Poly1305(key).decrypt(nonce, ct + tag, None)
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

        nonce = bytearray(12)
        nonce[0] = 2
        nonce[1:5] = self._hub_session
        nonce[8] = seq & 0xFF
        nonce[9] = (seq >> 8) & 0xFF
        nonce[10] = (seq >> 16) & 0xFF
        nonce[11] = (seq >> 24) & 0xFF
        plain = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        out = ChaCha20Poly1305(key).encrypt(bytes(nonce), plain, None)
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

        if mtype in ("hello", "lease", "state"):
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
        for cb in list(self._status_listeners):
            self.hass.add_job(cb, status)

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
        msg = {
            "v": 1,
            "type": "cmd",
            "id": dev_id,
            "class": dev_class,
            "seq": self._seq,
            "payload": self._encode_payload(dev_id, self._seq, payload),
        }
        self._sock.sendto(json.dumps(msg, separators=(",", ":")).encode("utf-8"), (ip, PORT))
