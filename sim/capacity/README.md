# Isolated encrypted ET-Core capacity ramp

Runs the real ETCore device library in separate Linux processes against the real
hub.py with Home Assistant framework objects stubbed. No HA API, production key,
real relay, firmware upload or LAN command is used.

This is a separate harness so existing protocol/hardware tests are unchanged.
The wrappers require UDP port **18560**, loopback multicast interface and TTL zero.
Both the Python hub wrapper and C++ UDP shim reject non-loopback destinations
(except the local multicast group). LAN mode is disabled. Keys are random and
throwaway. Do not remove these isolation guards to turn this into a live test.

```sh
cd sim/capacity
sh build.sh
nice -n 10 python3 -u capacity_ramp.py
```

Needs g++, OpenSSL development headers, ArduinoJson 7, and Python cryptography.
`ARDUINOJSON` can override the include directory. `ETCORE_CAPACITY_OUTPUT` selects
where JSON results are saved; the default is the ignored `results/` directory.

Stages: 25, 50, 100, 150, 250 devices, sequentially. Every device has an independent
loopback address and encryption identity. Leases are every 2 seconds, TTL 10 seconds,
and an encrypted state report is generated every 10 seconds. The `sample_seq`
field and device stdout counters allow detecting missing generated state reports.
The state-reporting wrapper does not modify production ETCore.cpp.

Each stage includes a 30-second steady window, virtual command ACKs to up to 50
devices, replacement of the simulated hub object (not a real HA restart), and ten
simulated abrupt device unplug events. The ramp stops after a failed stage.
Metrics include packet rate, process CPU, RSS, loop scheduling delay, report loss,
ACK latency, restart recovery and false offline events. RSS summed across devices
includes shared pages and is not unique physical RAM consumption. The Python CPU
measurement includes recording/monitoring harness overhead. This test uses the
portable UDP host shim, not ESP32 CPU/heap execution or the ESP-IDF socket driver.

A pass establishes only this workload on this host. It does not establish Wi-Fi
station capacity, airtime, real device latency, full HA entity/recorder capacity,
a production HA VM limit, or unlimited protocol capacity. Large bridge packets or
more frequent state updates require a separate workload test.

## Extended ceiling search

`python3 capacity_ceiling.py` tests 500, 1000, 2000, 4000, 8000 and 16000 devices,
with a 60-second steady window per level. It stops after the first failed level.
`ETCORE_CEILING_COUNTS=750,875` can select intermediate levels to investigate a
boundary; `ETCORE_CEILING_OUTPUT` selects a separate output directory. Join/recovery
checks take one device-table snapshot per check, avoiding a quadratic benchmark
artifact. This does not modify protocol/firmware code or any live configuration.

The search reserves at least 4 GiB of available host memory, 2048 cgroup PID slots
(where exposed) and stops spawning when the host's one-minute load exceeds 85%
of logical CPU count. A host resource guard is not an ET-Core protocol limit.
These guards do not turn this into a production stress tool. Keep the test on
loopback, at low scheduler priority, and do not run it on an already overloaded
machine. Larger simulator counts also introduce kernel scheduling/multicast work;
interpret failures by phase rather than claiming a universal device maximum.

## Reduce observer effects at high counts

`python3 capacity_bounded.py` uses the same protocol/device workload and OS socket
settings, but replaces unbounded raw-packet collection with counters, bounds the
fake event bus, yields while collecting publication counters and samples child
RSS outside the measurement/loss window. It also records per-socket kernel UDP
drops and the unchanged SO_RCVBUF value. This separates receiver behavior from
pauses introduced by test instrumentation. It does not increase socket buffers
or alter production protocol code. Raw-collector results remain separate.
`ETCORE_CEILING_COUNTS` selects counts; `ETCORE_BOUNDED_OUTPUT` selects the output
folder. The default starts at 2000 and increases until a failure.

`python3 capacity_staggered.py` uses the bounded collector and pauses 100 ms per
ten process starts, allowing the hub to process discovery while more clients are
created. It starts at 3000 by default and retains the same reporting rates,
protocol library, socket buffers and resource guards. Results are a different
startup scenario, not a replacement for simultaneous-start measurements.
