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
