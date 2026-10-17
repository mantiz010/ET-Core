"""ET-Core hardware test: the real hub.py on this PC's LAN against a real ESP32.

    python3 hw_test.py <psk_file> [device_id]

Steps (log lines are timestamped): wait for the device; 20 commands with ack
timing; hub restart (as if Home Assistant restarted) then a command; a replayed
command must not run; then waits for you to unplug the device (time to offline)
and plug it back in (time to online). Leaves your Home Assistant alone: ET-Core
uses its own port 5560 and group 239.10.0.2.
"""
from __future__ import annotations

import asyncio
import json
import socket
import statistics
import sys
import time

from harness import MemoryCounterStore, PORT, make_hub, wait_for

PSK = open(sys.argv[1]).read().strip()
DEV = sys.argv[2] if len(sys.argv) > 2 else "hwtest1"
T0 = time.monotonic()


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:6.1f}s] {msg}", flush=True)


def dev(hub):
    return hub.devices.get(DEV, {})


async def start_hub(loop, store):
    hub = make_hub(loop, PSK, store, lan=True)
    hub._last_state = {}
    hub.register_listener(lambda m: hub._last_state.__setitem__(m["id"], m["payload"])
                          if m.get("type") == "state" else None)
    hub.events = []
    hub.register_status_listener(lambda st: hub.events.append((time.monotonic(), st)))
    await hub.async_start()
    return hub


async def main():
    loop = asyncio.get_running_loop()
    store = MemoryCounterStore()
    hub = await start_hub(loop, store)
    log(f"hub up on UDP {PORT}, waiting for {DEV} (power it on / reset it)")
    if not await wait_for(lambda: dev(hub).get("online"), 3600, 0.1):
        log("FAIL device never came online in 60 min"); return
    d = dev(hub)
    log(f"ONLINE {DEV} at {d.get('last_addr')} name={d.get('name')!r} fw={d.get('fw')} rssi={d.get('rssi')} "
        f"lease_ttl={d.get('lease_ttl')}")
    await asyncio.sleep(3)
    st = hub._last_state.get(DEV, {})
    log(f"state: {st}")

    # 1. Commands
    # A command leaves _pending when acked *or* when it finally fails, so count
    # failures from the hub's fail counter, not from _pending alone.
    lat, fail = [], 0
    for i in range(20):
        f0 = hub._cmd_fail_count
        t = time.monotonic()
        hub.send_command(DEV, "switch.relay", {"on": i % 2 == 0})
        cid = str(hub._seq)
        done = await wait_for(lambda: cid not in hub._pending, 2, 0.002)
        if done and hub._cmd_fail_count == f0:
            lat.append((time.monotonic() - t) * 1000)
        else:
            fail += 1
        await asyncio.sleep(0.3)
    await asyncio.sleep(1)
    st = hub._last_state.get(DEV, {})
    log(f"COMMANDS 20 sent: {len(lat)} acked, failed {fail}, ack median {statistics.median(lat):.1f} ms "
        f"max {max(lat):.1f} ms; device counted {st.get('commands')}; heap {st.get('heap')} min {st.get('min_heap')}"
        if lat else f"COMMANDS: none acked, failed {fail}")

    # 2. Replay: resend the last command packet as captured
    raw = [r for r, a in hub.rec.tx if a[0] == dev(hub).get("last_addr") and b'"cmd"' in r][-1]
    before = hub._last_state.get(DEV, {}).get("commands")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for _ in range(3):
        s.sendto(raw, (dev(hub)["last_addr"], PORT))
        await asyncio.sleep(0.3)
    s.close()
    hub.send_command(DEV, "switch.relay", {"on": False})  # makes the device report its count
    await asyncio.sleep(1.5)
    after = hub._last_state.get(DEV, {}).get("commands")
    log(f"REPLAY 3 recorded copies: device count {before} -> {after} "
        f"({'OK, replay not executed' if after == (before or 0) + 1 else 'FAIL'})")

    # 3. Hub restart (Home Assistant restart)
    await hub.async_stop()
    log("hub stopped (simulated HA restart), starting again")
    t = time.monotonic()
    hub = await start_hub(loop, store)
    ok_online = await wait_for(lambda: dev(hub).get("online"), 15, 0.05)
    ok_name = await wait_for(lambda: dev(hub).get("name") == "ET-Core HW Test", 5, 0.05)
    log(f"RESTART: online again after {time.monotonic() - t:.1f} s ({ok_online}), name relearned: {ok_name}")
    hub.send_command(DEV, "switch.relay", {"on": True})
    cid = str(hub._seq)
    log(f"RESTART: command after restart acked: {await wait_for(lambda: cid not in hub._pending, 2, 0.002)}")

    # 4. Unplug / replug
    log(">>> NOW UNPLUG the ESP32 (pull USB power). Waiting up to 10 min.")
    if not await wait_for(lambda: not dev(hub).get("online"), 600, 0.05):
        log("no unplug seen"); return
    last_seen_ago = time.time() - float(dev(hub).get("last_seen") or 0)
    log(f"OFFLINE detected {last_seen_ago:.1f} s after the last packet from the device (lease ttl {dev(hub).get('lease_ttl')} s)")
    log(">>> NOW PLUG IT BACK IN. Waiting up to 10 min.")
    t = time.monotonic()
    if not await wait_for(lambda: dev(hub).get("online"), 600, 0.05):
        log("did not come back"); return
    log(f"ONLINE again {time.monotonic() - t:.1f} s after offline (includes your time to plug in + boot + WiFi)")
    await asyncio.sleep(2)
    hub.send_command(DEV, "switch.relay", {"on": True})
    cid = str(hub._seq)
    log(f"after replug command acked: {await wait_for(lambda: cid not in hub._pending, 3, 0.002)}")
    log(f"hub totals: rx {hub._rx_count} decrypt_fail {hub._decrypt_fail_count} replays_dropped {hub._replay_count} "
        f"cmd_failed {hub._cmd_fail_count}")
    log("DONE")
    await hub.async_stop()


asyncio.run(main())
