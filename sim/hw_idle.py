"""ET-Core idle test on real hardware: the device is left alone (no commands).

    python3 hw_idle.py <psk_file> [minutes] [device_id]

An idle device hears nothing from the hub except multicast announces (often lost on
WiFi). After 100 s without the hub it searches again (multicast lease with nohub) and
the hub answers. Checks: no false offline while idle, every search answered, and a
command after the idle period works at normal latency.
"""
from __future__ import annotations

import asyncio
import sys
import time

from harness import MemoryCounterStore, make_hub, wait_for

PSK = open(sys.argv[1]).read().strip()
MIN = float(sys.argv[2]) if len(sys.argv) > 2 else 10
DEV = sys.argv[3] if len(sys.argv) > 3 else "hwtest1"
T0 = time.monotonic()


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:6.1f}s] {msg}", flush=True)


async def main():
    hub = make_hub(asyncio.get_running_loop(), PSK, MemoryCounterStore(), lan=True)
    status = []
    hub.register_status_listener(lambda st: status.append((time.strftime('%H:%M:%S'), st["id"], st["reason"])))
    searches = []
    orig = hub._record_message

    def rec(msg, ip):
        if msg.get("id") == DEV and isinstance(msg.get("payload"), dict) and msg["payload"].get("nohub"):
            searches.append(time.monotonic() - T0)
        orig(msg, ip)

    hub._record_message = rec
    await hub.async_start()
    if not await wait_for(lambda: hub.devices.get(DEV, {}).get("online"), 30, 0.1):
        log("device not online"); return
    status.clear()
    log(f"online; idle for {MIN} min, no commands")
    end = time.monotonic() + MIN * 60
    while time.monotonic() < end:
        await asyncio.sleep(60)
        log(f"idle: searches {len(searches)}, answered {hub._nohub_count}, status events {status}")
    t = time.monotonic()
    hub.send_command(DEV, "switch.relay", {"on": True})
    cid = str(hub._seq)
    ok = await wait_for(lambda: cid not in hub._pending, 2, 0.002)
    log(f"command after idle: acked {ok} in {(time.monotonic() - t) * 1000:.1f} ms")
    gaps = [round(b - a) for a, b in zip(searches, searches[1:])]
    log(f"RESULT: {MIN} min idle, device searches {len(searches)} (gaps s {gaps}), hub answered "
        f"{hub._nohub_count}, offline/online events while idle {status}, "
        f"commands failed {hub._cmd_fail_count}")
    await hub.async_stop()


asyncio.run(main())
