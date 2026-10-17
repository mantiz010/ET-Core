# ET-Core simulator results — 2026-10-17 (branch core-fixes)

Real `ETCore.cpp` devices (one Linux process each) against the real `hub.py`, encrypted,
on loopback. Hub-side load only: loopback has no WiFi airtime or multicast cost.
Machine: 32 cores (Kate also running).

| Devices | Lease / TTL | All online | Packets/s into hub | Hub CPU (1 core) | False offlines (30 s) | Cmd ack median / max | Unplug -> offline |
|---|---|---|---|---|---|---|---|
| 50  | 2 s / 6 s | 0.2 s | 25  | 0.5% | 0 | 3.1 / 5.1 ms | 4.7 s |
| 100 | 2 s / 6 s | 2.1 s | 50  | 1.0% | 0 | 2.9 / 4.8 ms | 4.9 s |
| 250 | 2 s / 6 s | 2.2 s | 125 | 2.7-3.1% | 0 | 3.0 / 5.0 ms | 4.9 s (12 runs) |
| 250 | 3 s / 9 s | 3.2 s | 83  | 1.8% | 0 | 3.1 / 3.3 ms | 8.9 s |

50 commands per run, all acked, no resends needed. 10 devices unplugged at once per run,
10/10 detected every run, no other device falsely offline.

One early 250 run reported no unplug result: the measuring script read the hub's
online flag before the status callbacks ran (script race, fixed in scale.py; the hub had
marked all 10 offline). 12 later runs clean.

Not measured here (needs real hardware/WiFi): airtime of 125 small unicast packets/s on
one access point, ESP32 CPU/heap with encryption, multicast announce delivery on WiFi.

Tests: `python3 -m unittest -v test_etcore` — 20/20.

Not run yet: the Home Assistant entity layer (`__init__.py`, switch/light/sensor
platforms, config flow, panel). The harness loads only `hub.py`; no Docker or
Python 3.12+ on this PC to run a test Home Assistant. Known issue there: the sensor
platform makes an entity from every field of every packet type (lease ttl/uptime/rssi,
ack cmd_id/ok), not just `state`.

## Hardware: ESP32-C3 ("hwtest1", examples/HWTest), 2026-10-17

Real hub.py on the LAN (this PC), encrypted, device on WiFi (RSSI -55 to -77 dBm).

| Test | Result |
|---|---|
| Join + first lease | online, name/fw learned |
| Unplug -> offline | 7.0 s after the last packet (ET-Bus 1.7: 42 s) |
| Quick replug (< 6 s) | never shown offline (correct: within the lease TTL) |
| Simulated HA restart | online again 0.6 s, name relearned, next command acked |
| HA restored from backup (hub seq behind) | stale ack -> hub moved up -> command ran, < 1 s |
| 15 OTA reboots x 20 commands right after boot | 300/300, 0 failed, 0 unexpected restarts |
| 20 min, 1 command/s | 1198/1198, 0 failed, 0 restarts |
| Free heap | 186.7 KB steady; lowest ever 174 KB |
| Typical ack | ~10 ms |

Open: in the first two runs (first boot after USB flash, and one OTA cycle right after a
78 s OTA) 11-19 of 20 commands right after boot failed and the device's command count
restarted, which looks like an unseen restart or a WiFi stall. Not reproduced in ~40
later reboots and ~1800 commands, including after adding reset-reason reporting. Signal
was weaker then (-69 to -77 dBm vs -55). Keep hw_soak.py running on future firmware.
