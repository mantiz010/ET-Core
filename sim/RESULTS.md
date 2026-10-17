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

## Home Assistant 2026.10.1 (throwaway instance on this PC) + real ESP32-C3

The integration finally ran inside Home Assistant (same version as production).

| Check | Result |
|---|---|
| Config flow with PSK | entry created, hub listening |
| Entity | `switch.hwtest1`, named from the device hello ("ET-Core HW Test") |
| HA on/off x10 | 10/10; HA service call -> state reported back by the device: ~23 ms |
| Device silent (traffic blocked) | unavailable after 5.0-5.8 s |
| Device back | available again after 1.1-1.3 s with the right state |
| HA restart with the device ON | shows "on" 1.2 s after HA's API is up |
| HA log | no ET-Core errors |

Bugs this found and fixed (72faad4 + next): entity kept the raw id as name; sensor
platform made entities from lease/ack fields; state wrong for up to 60 s after an HA
restart (onSync); device registry name not updated; multicast announce never reached
the C3 over WiFi (unicast hello request). Scripts: tools/ha_e2e.py, tools/ha_restart_check.sh.
Not covered yet: the light platform with a real RGB device, the sidebar panel UI.

## 0.3.0: closing the open items (2026-10-17)

Changes (each with a simulator test that failed first, except the HA/panel ones,
which were found by running them):

| Item | Before | Now |
|---|---|---|
| Announce | plaintext, anyone could point devices elsewhere | encrypted (site key for multicast, device key + `_ds` for unicast), carries the hub address; device follows it only from that address |
| Earlier-boot lease replay | could keep an unplugged device online | new sessions must echo a hub challenge (`ch`) first |
| hello | plaintext, a forged one could rename a device | encrypted; hub drops all plaintext with a key set |
| Session | 32 bits (nonce reuse possible over many reboots) | 56 bits |
| HA moves address | devices kept unicasting to the old one forever | after 100 s without the hub: multicast `nohub` lease, hub answers |
| Planned restart | offline only when the lease ran out (~6 s) | `bye`: offline at once |
| Light | untested, no effects | effects from the device hello (`onHello`), tested in HA |
| Sidebar panel | 401 in a real browser | works (checked with headless Chromium) |
| Name after HA restart | could fall back to the raw id | device registered with the hello name |
| HA 2026.10 | deprecated `async_get_device` (removed 2027.8) | `async_get_device_by_identifier` |
| decrypt_failures | also counted replays/unproven | only real failures |

Simulator: 32/32 tests. Scale, 250 devices (loopback): all online 2.6 s, 125 pkt/s,
hub 2.7% of a core, 0 false offline, acks ~3 ms, HA restart -> all 250 online, named
and with state in 2.0 s (hub sends ~340 packets for it), unplug -> offline 4.7-6.0 s.

ESP32-C3 on WiFi, real hub on the LAN (sim/hw_check.py):

| Check | Result |
|---|---|
| New hub -> online + name + state | 0.4-0.5 s (challenge round trip included) |
| 20 commands | 20/20, ack median 9-12 ms |
| Hub restart (as HA restart) | online + name + state 0.4-1.8 s |
| OTA start -> offline (bye) | 1.7 s after espota starts (its login takes that long) |
| After OTA -> new boot online | 2.2 s after flashing ends |
| Counters | decrypt failures 0, replays 0, commands failed 0 |

Home Assistant 2026.10.1 (throwaway, this PC) with the C3:

| Check | Result |
|---|---|
| Switch on/off x10 | 10/10, ~24 ms service call -> device state |
| Switch silent -> unavailable / back | 6.0 s / 1.6 s |
| Switch after HA restart | real state 2.1-2.7 s after the API is up, name "ET-Core HW Test" |
| Light (LightTest sketch) on/off, brightness, RGB, 3 effects | 10/10, ~26 ms each |
| Light after HA restart | real state (green, brightness 120) 1.4 s after the API is up |
| Sidebar panel | loads, shows device, counters and events |
| HA log | no ET-Core warnings or errors |
