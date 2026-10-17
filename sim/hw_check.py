"""ET-Core hardware check after protocol changes, against the real C3 (no unplugging).

    python3 hw_check.py <psk_file> <ota_pass_file> <firmware.bin> [device_id]

1. hub start -> device online / name / state (device first heard by a new hub:
   new-session challenge round trip included)
2. 20 commands, ack timing
3. hub restart (as HA restart) -> online / name / state again
4. OTA the same firmware: time from OTA start to offline (bye), then back online
5. 20 commands on the new boot; hub counters
"""
from __future__ import annotations

import asyncio
import glob
import os
import statistics
import sys
import time

from harness import MemoryCounterStore, make_hub, wait_for

PSK = open(sys.argv[1]).read().strip()
OTA = open(sys.argv[2]).read().strip()
FW = sys.argv[3]
DEV = sys.argv[4] if len(sys.argv) > 4 else "hwtest1"
ESPOTA = glob.glob(os.path.expanduser("~/.arduino15/packages/esp32/hardware/esp32/*/tools/espota.py"))[0]
T0 = time.monotonic()


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:6.1f}s] {msg}", flush=True)


async def start(store):
    hub = make_hub(asyncio.get_running_loop(), PSK, store, lan=True)
    hub.state = None
    hub.register_listener(lambda m: setattr(hub, "state", m["payload"]) if m.get("type") == "state" and m.get("id") == DEV else None)
    t = time.monotonic()
    await hub.async_start()
    d = lambda: hub.devices.get(DEV, {})
    r = {}
    for what, pred in (("online", lambda: d().get("online")), ("name", lambda: d().get("name")),
                       ("state", lambda: hub.state is not None)):
        ok = await wait_for(pred, 30, 0.01)
        r[what] = round(time.monotonic() - t, 2) if ok else None
    return hub, r


async def burst(hub, n=20, gap=0.3):
    f0, lat = hub._cmd_fail_count, []
    for i in range(n):
        t = time.monotonic()
        hub.send_command(DEV, "switch.relay", {"on": i % 2 == 0})
        cid = str(hub._seq)
        await wait_for(lambda: cid not in hub._pending, 2, 0.002)
        lat.append((time.monotonic() - t) * 1000)
        await asyncio.sleep(gap)
    await asyncio.sleep(1.5)
    return hub._cmd_fail_count - f0, statistics.median(lat), max(lat)


async def main():
    store = MemoryCounterStore()
    hub, r = await start(store)
    log(f"1 new hub: online {r['online']} s, name {r['name']} s, state {r['state']} s "
        f"(name={hub.devices.get(DEV, {}).get('name')!r})")
    if r["online"] is None:
        return
    f, med, mx = await burst(hub)
    log(f"2 commands: 20 sent, failed {f}, ack median {med:.1f} ms max {mx:.1f} ms")
    await hub.async_stop()
    hub, r = await start(store)
    log(f"3 hub restart: online {r['online']} s, name {r['name']} s, state {r['state']} s")
    ip, boot0 = hub.devices[DEV]["last_addr"], hub.devices[DEV].get("boot")
    t = time.monotonic()
    p = await asyncio.create_subprocess_exec("python3", ESPOTA, "-i", ip, "-a", OTA, "-f", FW, "-t", "10",
                                             stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    bye = lambda: [e for _, n, e in hub.hass.bus.events if n == "etcore_device_status" and e["reason"] == "bye"]
    got_bye = await wait_for(bye, 20, 0.01)
    t_bye = time.monotonic() - t
    await p.wait()
    t_flash = time.monotonic() - t
    back = await wait_for(lambda: hub.devices[DEV].get("online") and hub.devices[DEV].get("boot") != boot0, 90, 0.01)
    log(f"4 OTA: bye seen {got_bye} {t_bye:.2f} s after OTA start; flash took {t_flash:.1f} s (rc {p.returncode}); "
        f"new boot online {back} {time.monotonic() - t - t_flash:.1f} s after flashing ended")
    await asyncio.sleep(1)
    f, med, mx = await burst(hub)
    log(f"5 commands on new boot: failed {f}, ack median {med:.1f} ms max {mx:.1f} ms")
    s = hub.get_diag_snapshot()
    log(f"hub: rx {s['rx_count']} decrypt_fail {s['decrypt_failures']} replays {s['replays_dropped']} "
        f"unproven {s['unproven_dropped']} cmd_failed {s['commands_failed']}")
    await hub.async_stop()


asyncio.run(main())
