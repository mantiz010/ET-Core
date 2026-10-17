"""ET-Core hardware soak test against a real ESP32 running examples/HWTest (with OTA).

    python3 hw_soak.py <psk_file> <ota_password_file> <firmware.bin> [reboot_cycles] [steady_minutes]

Phase 1: OTA-reboot the device `reboot_cycles` times; after each boot send 20 commands
0.3 s apart. Phase 2: one command per second for `steady_minutes`.
Any change of the device's boot id that the test did not cause is reported with the
ESP32 reset reason from the device's state (1 power-on, 3 software, 4 panic,
5-7 watchdog, 9 brownout). Command failures come from the hub's fail counter.
"""
from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import sys
import time

from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

from harness import MemoryCounterStore, make_hub, wait_for

PSK = open(sys.argv[1]).read().strip()
OTA = open(sys.argv[2]).read().strip()
FW = sys.argv[3]
CYCLES = int(sys.argv[4]) if len(sys.argv) > 4 else 10
STEADY_MIN = float(sys.argv[5]) if len(sys.argv) > 5 else 10
DEV = "hwtest1"
ESPOTA = "/home/mantiz010/.arduino15/packages/esp32/hardware/esp32/3.3.11/tools/espota.py"
KEY = hashlib.sha256(bytes.fromhex(PSK) + DEV.encode()).digest()


def log(m):
    print(f"[{time.strftime('%H:%M:%S')}] {m}", flush=True)


def dec(m):
    p = m.get("payload") or {}
    if p.get("_enc") != 1:
        return p
    try:
        return json.loads(ChaCha20Poly1305(KEY).decrypt(
            base64.b64decode(p["n"]), base64.b64decode(p["ct"]) + base64.b64decode(p["tag"]),
            f'v1|{m["type"]}|{m["id"]}|{m["seq"]}'.encode()))
    except Exception as e:  # noqa: BLE001
        return f"DECRYPT FAIL {e!r}"


class Watch:
    """Tracks boot ids and the latest state; flags boots the test did not cause."""

    def __init__(self, hub):
        self.hub, self.expected, self.boots, self.state = hub, set(), [], {}
        self.unexpected = []
        self.allow_next = False  # set before an OTA: the next new boot is ours
        hub.register_listener(self.on_msg)

    def on_msg(self, m):
        if m.get("id") != DEV:
            return
        b = m.get("boot")
        if b and (not self.boots or self.boots[-1] != b):
            self.boots.append(b)
            if self.allow_next:
                self.expected.add(b)
                self.allow_next = False
            elif b not in self.expected and len(self.boots) > 1:
                self.unexpected.append((time.strftime('%H:%M:%S'), b))
                log(f"!!! UNEXPECTED RESTART: new boot {b}")
        if m.get("type") == "state" and isinstance(m.get("payload"), dict):
            self.state = m["payload"]
            if self.unexpected and self.unexpected[-1][1] == b and "reset" in self.state:
                log(f"!!! reset reason {self.state.get('reset')} (uptime {self.state.get('up')} s)")


async def ota(ip):
    p = await asyncio.create_subprocess_exec("python3", ESPOTA, "-i", ip, "-a", OTA, "-f", FW, "-t", "10",
                                             stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
    try:
        await asyncio.wait_for(p.communicate(), 150)
    except asyncio.TimeoutError:
        p.kill()
        return False
    return p.returncode == 0


async def burst(hub, n, gap):
    f0 = hub._cmd_fail_count
    for i in range(n):
        hub.send_command(DEV, "switch.relay", {"on": i % 2 == 0})
        await asyncio.sleep(gap)
    await asyncio.sleep(2)
    return hub._cmd_fail_count - f0


async def main():
    hub = make_hub(asyncio.get_running_loop(), PSK, MemoryCounterStore(), lan=True)
    w = Watch(hub)
    await hub.async_start()
    w.expected.add("__first__")
    if not await wait_for(lambda: hub.devices.get(DEV, {}).get("online"), 30, 0.1):
        log("device not online"); return
    total_fail = total_cmd = 0
    for k in range(CYCLES):
        ip, boot0 = hub.devices[DEV]["last_addr"], hub.devices[DEV].get("boot")
        w.allow_next = True
        ok = await ota(ip)
        if not await wait_for(lambda: hub.devices[DEV].get("online") and hub.devices[DEV].get("boot") != boot0, 120, 0.01):
            log(f"cycle {k+1}: device did not come back after OTA ({ok})"); break
        f = await burst(hub, 20, 0.3)
        total_fail += f; total_cmd += 20
        log(f"cycle {k+1}: OTA {'ok' if ok else 'FAILED'}, 20 cmds after boot, failed {f}, "
            f"count {w.state.get('commands')} heap {w.state.get('heap')} reset {w.state.get('reset')}")
    log(f"PHASE 1: {CYCLES} reboots, {total_cmd} commands, {total_fail} failed, unexpected restarts {len(w.unexpected)}")
    end, f0, n = time.monotonic() + STEADY_MIN * 60, hub._cmd_fail_count, 0
    heap_min = None
    while time.monotonic() < end:
        hub.send_command(DEV, "switch.relay", {"on": n % 2 == 0})
        n += 1
        await asyncio.sleep(1)
        h = w.state.get("min_heap")
        if h is not None:
            heap_min = h if heap_min is None else min(heap_min, h)
        if n % 120 == 0:
            log(f"steady: {n} cmds, failed {hub._cmd_fail_count - f0}, heap {w.state.get('heap')} "
                f"min {w.state.get('min_heap')} up {w.state.get('up')} s")
    await asyncio.sleep(2)
    log(f"PHASE 2: {n} commands over {STEADY_MIN} min, failed {hub._cmd_fail_count - f0}, "
        f"lowest free heap {heap_min}, unexpected restarts total {len(w.unexpected)} {w.unexpected}")
    log(f"hub: rx {hub._rx_count} decrypt_fail {hub._decrypt_fail_count} replays {hub._replay_count}")
    await hub.async_stop()


asyncio.run(main())
