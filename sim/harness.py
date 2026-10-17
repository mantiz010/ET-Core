"""Run the real ET-Core hub.py without Home Assistant, on loopback.

Only the two names hub.py imports from Home Assistant are stubbed. Multicast is
kept on this machine (loopback interface, TTL 0)."""
from __future__ import annotations

import asyncio
import importlib
import json
import os
import socket
import subprocess
import sys
import time
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENT = HERE.parent / "homeassistant" / "custom_components" / "etcore"
DEVICE_BIN = HERE / "device_sim"
# The simulator uses its own port so it never mixes with real ET-Core devices or a
# test Home Assistant on this machine (both use 5560). device_sim is built with it.
GROUP = "239.10.0.2"
PORT = int(os.environ.get("ETCORE_SIM_PORT", "15560"))


def load_hub_module():
    if "etcore.hub" in sys.modules:
        return sys.modules["etcore.hub"]
    ha = types.ModuleType("homeassistant")
    core = types.ModuleType("homeassistant.core")

    class HomeAssistant:  # noqa: D401 - stand-in type only
        pass

    core.HomeAssistant = HomeAssistant
    core.callback = lambda f: f
    sys.modules.setdefault("homeassistant", ha)
    sys.modules.setdefault("homeassistant.core", core)
    pkg = types.ModuleType("etcore")
    pkg.__path__ = [str(COMPONENT)]  # submodules load without running __init__.py
    sys.modules["etcore"] = pkg
    return importlib.import_module("etcore.hub")


class FakeBus:
    def __init__(self):
        self.events: list[tuple[float, str, dict]] = []

    def async_fire(self, name, data):
        self.events.append((time.monotonic(), name, dict(data)))


class FakeHass:
    def __init__(self, loop):
        self.loop = loop
        self.bus = FakeBus()

    def add_job(self, cb, *args):
        self.loop.call_soon(cb, *args)


class Recorder:
    """Every datagram the hub received (rx) and sent (tx), raw bytes + address."""

    def __init__(self):
        self.rx: list[tuple[bytes, tuple]] = []
        self.tx: list[tuple[bytes, tuple]] = []


class MemoryCounterStore:
    """Stands in for Home Assistant's Store: survives a hub restart within one test."""

    def __init__(self):
        self.value = 0

    async def async_load(self):
        return self.value

    async def async_save(self, value):
        self.value = int(value)


def make_hub(loop, psk: str | None = None, store=None, lan: bool = False):
    """lan=True: talk on the real network (hardware tests) instead of loopback."""
    mod = load_hub_module()

    port = 5560 if lan else PORT
    mod.PORT = port  # hub.py sends and binds with this module-level constant

    class LoopbackHub(mod.EtCoreHub):
        def _build_socket(self):
            sock = super()._build_socket()
            if lan:
                return sock
            lo = socket.inet_aton("127.0.0.1")
            try:
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(GROUP) + lo)
            except OSError:
                pass
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, lo)
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 0)
            return sock

        # drop_rx / drop_tx: number of packets of a type to lose ("network loss").
        def _handle_datagram(self, data, addr):
            self.rec.rx.append((bytes(data), addr))
            if self._lose(self.drop_rx, data):
                return
            super()._handle_datagram(data, addr)

        def _send_raw(self, data, addr):
            self.rec.tx.append((bytes(data), addr))
            if self._lose(self.drop_tx, data):
                return
            super()._send_raw(data, addr)

        @staticmethod
        def _lose(rules, data):
            try:
                mtype = json.loads(data).get("type")
            except ValueError:
                return False
            if rules.get(mtype, 0) > 0:
                rules[mtype] -= 1
                return True
            return False

    hub = LoopbackHub(FakeHass(loop), {"psk_hex": psk} if psk else {}, counter_store=store)
    hub.rec = Recorder()
    hub.drop_rx, hub.drop_tx = {}, {}
    return hub


class Device:
    """A simulated device process running the real ETCore library."""

    def __init__(self, dev_id, dev_class, ip, psk=None, lease_ms=None, ttl=None, env=None):
        args = [str(DEVICE_BIN), dev_id, dev_class, ip, psk or "-"]
        if lease_ms is not None:
            args += [str(lease_ms)]
            if ttl is not None:
                args += [str(ttl)]
        self.id, self.ip = dev_id, ip
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, text=True, bufsize=1,
                                     env={**os.environ, **(env or {})})
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.lines: list[str] = []

    def poll_lines(self):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                break
            self.lines.append(line.rstrip("\n"))
        return self.lines

    def commands(self):
        return [l[4:] for l in self.poll_lines() if l.startswith("CMD ")]

    def kill(self):
        """Pull the plug: no bye, no goodbye."""
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.kill()


def group_listener():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind((GROUP, PORT))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(GROUP) + socket.inet_aton("127.0.0.1"))
    s.setblocking(False)
    return s


def sender_from(ip):
    """A socket that sends as if from a device address (for replay tests)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind((ip, PORT))
    return s


async def wait_for(pred, timeout=10.0, step=0.05):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        await asyncio.sleep(step)
    return bool(pred())
