"""ET-Core scale test: N simulated devices (real ETCore.cpp) against the real hub.py.

    python3 scale.py 100            # 2 s lease / 6 s TTL (default profile)
    python3 scale.py 250 3000 9     # 3 s lease / 9 s TTL (large-site profile)

Measures on loopback: time until all online, hub CPU and packet rate in steady
state, false offlines, command ack latency, and offline detection after unplug.
Loopback has no WiFi airtime or multicast cost, so packet rates are what the hub
must handle, not what a real access point would cope with.
"""
from __future__ import annotations

import asyncio
import os
import random
import statistics
import sys
import time

from harness import Device, MemoryCounterStore, make_hub, wait_for

N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
LEASE_MS = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
TTL = int(sys.argv[3]) if len(sys.argv) > 3 else 6
PSK = os.urandom(32).hex()
STEADY_S = 30


def ip_for(i):
    return f"127.0.{1 + i // 250}.{1 + i % 250}"


async def main():
    loop = asyncio.get_running_loop()
    hub = make_hub(loop, PSK, MemoryCounterStore())
    await hub.async_start()
    offline_events = []
    hub.register_status_listener(lambda st: offline_events.append((time.monotonic(), st)))

    t0 = time.monotonic()
    devs = [Device(f"dev{i:03d}", "switch.relay", ip_for(i), PSK, LEASE_MS, TTL) for i in range(N)]
    online = lambda: sum(1 for d in devs if hub.devices.get(d.id, {}).get("online"))
    ok = await wait_for(lambda: online() == N, 60, 0.2)
    t_all = time.monotonic() - t0
    print(f"devices: {N}  lease {LEASE_MS} ms / ttl {TTL} s")
    print(f"all online: {'yes' if ok else 'NO (%d/%d)' % (online(), N)} in {t_all:.1f} s")

    # Steady state
    await asyncio.sleep(3)
    offline_events.clear()
    rx0, cpu0, w0 = len(hub.rec.rx), time.process_time(), time.monotonic()
    await asyncio.sleep(STEADY_S)
    rx1, cpu1, w1 = len(hub.rec.rx), time.process_time(), time.monotonic()
    false_off = [st for _, st in offline_events if not st["online"]]
    print(f"steady {STEADY_S} s: {(rx1 - rx0) / (w1 - w0):.1f} packets/s into the hub, "
          f"hub CPU {100 * (cpu1 - cpu0) / (w1 - w0):.1f}% of one core, false offlines: {len(false_off)}")

    # Commands: one to each of 50 random devices, ack latency
    targets = random.sample(devs, min(50, N))
    lat, fails = [], 0
    for d in targets:
        n_before = len(d.commands())
        t = time.monotonic()
        hub.send_command(d.id, "switch.relay", {"on": True})
        acked = await wait_for(lambda: not hub._pending, 2, 0.002)
        if acked and await wait_for(lambda: len(d.commands()) > n_before, 1, 0.01):
            lat.append((time.monotonic() - t) * 1000)
        else:
            fails += 1
    if lat:
        print(f"commands: {len(lat)}/{len(targets)} acked, ack latency median {statistics.median(lat):.1f} ms, "
              f"max {max(lat):.1f} ms, failed {fails}, resends {hub._cmd_fail_count} given up")

    # Unplug 10 devices at once, time each offline
    victims = random.sample(devs, min(10, N))
    t_kill = time.monotonic()
    for d in victims:
        d.kill()
    ids = {d.id for d in victims}
    # Wait for the status events themselves: the hub flips "online" first and the
    # listeners run a moment later, so polling the flag can read before they land.
    off_events = lambda: {st["id"] for _, st in offline_events if st["id"] in ids and not st["online"]}
    await wait_for(lambda: len(off_events()) == len(ids), TTL + 10, 0.05)
    det = sorted(t - t_kill for t, st in offline_events if st["id"] in ids and not st["online"])
    if not det:
        print(f"unplug 10: NO offline events within {TTL + 10} s; still online: "
              f"{[i for i in ids if hub.devices.get(i, {}).get('online')]}; "
              f"exited: {[d.proc.poll() for d in victims]}")
    if det:
        print(f"unplug 10: offline after {min(det):.1f}-{max(det):.1f} s (ttl {TTL} s), "
              f"{len(det)}/10 detected; other devices falsely offline: "
              f"{sum(1 for _, st in offline_events if not st['online'] and st['id'] not in ids)}")
    print(f"hub totals: rx {hub._rx_count}, decrypt fail {hub._decrypt_fail_count}, replays dropped {hub._replay_count}")

    for d in devs:
        d.stop()
    await hub.async_stop()


asyncio.run(main())
