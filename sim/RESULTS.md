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

Changes. Simulator tests were written first and seen failing for: announce attacks,
earlier-boot replay, encrypted hello, 56-bit session, bye. Not seen failing first: the
hub-silent fallback test (the code did not exist yet, so it could not pass) and the
old-boot flood test (it passed on the old code too, by timing luck; the fix makes it
safe by design). HA/panel items were found by running them.

| Item | Before | Now |
|---|---|---|
| Announce | plaintext, anyone could point devices elsewhere | encrypted (site key for multicast, device key + `_ds` for unicast), carries the hub address; device follows it only from that address |
| Earlier-boot lease replay | could keep an unplugged device online | new sessions must echo a hub challenge (`ch`, HMAC, 10 s) first; replaced sessions retired |
| hello | plaintext, a forged one could rename a device | encrypted; hub drops all plaintext with a key set |
| Session | 32 bits (nonce reuse possible over many reboots) | 56 bits |
| HA moves address | devices kept unicasting to the old one forever | after 100 s without the hub: multicast `nohub` lease, hub answers (simulator only, 3 s timeout, hub at the same address; a real HA address change is untested) |
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

Later the same evening (head 34486e5, final 0.3.0 firmware on the C3):

| Check | Result |
|---|---|
| Soak: 10 OTA reboots x 20 commands, then 30 min at 1 command/s | 1997/1997, 0 failed, 0 unexpected restarts (all resets = OTA), lowest free heap 174 KB; the early "burst after boot" failures did not come back |
| hw_check on the final firmware | online 0.7 s, hub restart 1.6 s, bye on OTA, 40/40 commands, 0 decrypt failures |
| Idle 10 min, no commands | 0 false offlines; 0 hub searches, i.e. the encrypted multicast announce reached the C3 every time (so the 100 s fallback was never needed here and is still exercised by the simulator only); command after idle acked in 20 ms |

## ET-Core 0.3.0 vs ET-Bus 1.7.1, same C3, same test HA (2026-10-17)

ET-Bus ran on port 5556 so the real HA never saw it; both as an RGB light (ET-Bus only
creates switch entities for multi-switch boards, so a single switch could not be compared).
Scripts: tools/ha_bench_light.py, tools/ha_wire_rtt.py.

| | ET-Bus 1.7.1 | ET-Core 0.3.0 |
|---|---|---|
| Command round trip on the wire (HA packet out -> device reply in), median of 20, per run | 5.7 / 6.6 / 7.0 ms | 6.3 / 6.7 / 6.9 ms (was 7.8-10.8 ms before reading whole datagrams) |
| HA shows the new state | at once, *before* the device answers (optimistic, 3 ms) | when the device confirms it (~24 ms) |
| Device goes silent -> unavailable | 40.1 s | 5.4 s |
| Device back -> available | 9.9 s (waits for the next ping) | 1.0 s |
| HA restart -> state shown | 2.3 s, but it is the *saved* state, not asked from the device | 1.7 s, the device's real state |
| 30 light commands | 30/30 | 30/30 |
| Traffic, idle device | hub multicast ping every 10 s + device pong per ping | device lease every 2 s unicast, hub multicast announce every 30 s |

## ET-Bus feature parity (2026-10-17 evening)

Every ET-Bus device type, in the test HA 2026.10.1 with the C3 running
examples/ParityTest (one type per build, tools/ha_parity_e2e.py):

| Type | Checks |
|---|---|
| switch.multi, 3 named relays | 13/13: one entity each (named from the hello), each on/off, others unaffected |
| fan.speed | 4/4: set 40 %, off, on, on at 75 % |
| fan.preset (presets from the hello) | 4/4: preset list, turbo, off, on |
| sensor.air (co2, temp, humidity) | 4/4: values, units ppm/°C/%, device classes, state class measurement |
| sensor.temperature value style | 1/1: 19.25 °C |
| light.rgb without an effect list (like RGB1) | 6/6: ET-Bus's 35 effects offered, effect + colour, speed slider confirmed by the device |
| Device error (sendError) | logged once in HA, event fired |
| Options flow | bad key rejected, valid change reloads, light back in 0.7 s |
| etcore.send_command | command reached the device; unknown device -> clear error |
| Single switch after the switch rewrite | same entity id, 10/10 toggles, ~24 ms |

All 9 examples compile (incl. ports of the ET-Bus examples and the WiFi manager).
Simulator 36/36. hw_check after: 40/40 commands, decrypt failures 0.
Not ported on purpose: send_kate_command (Kate speaks ET-Bus), saved-state display.
