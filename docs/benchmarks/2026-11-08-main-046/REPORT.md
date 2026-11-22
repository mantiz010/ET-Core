# ET-Core 0.4.6: 1,000-device simulator validation

Tested on 2026-11-08 against main commit `bcb9863978ee9d82074661d2062b3520c7ae24cb`, using the real ETCore Arduino library, encryption implementation and Python hub. Source hashes and machine-readable results accompany this report.

Each simulated device ran in a separate Linux process on loopback UDP port 18560. Home Assistant framework, event bus and storage were stubbed. This validates the protocol/library/hub workload, not physical Wi-Fi capacity, ESP32 memory/CPU, the dashboard, recorder or full Home Assistant throughput. No production devices or actuators were contacted.

## Workload and result

- 1,000 encrypted devices; all joined in 4.45 seconds.
- Lease interval 2 seconds, offline TTL 10 seconds; state interval 10 seconds.
- 60-second steady measurement: 6,001 generated state reports, zero missing; approximately 600 received packets/second.
- 50 simulated commands: all acknowledged; p95 acknowledgement 3.45 milliseconds.
- Zero false offline events, decrypt failures or replay drops; zero kernel socket drops.
- Simulated hub restart: all devices recovered in 2.05 seconds.
- 10 simulated unplug events: all detected within 8.97–9.98 seconds.
- Hub process CPU: 17.57% of one logical CPU; this is host-specific and includes benchmark overhead.

The collector used bounded packet counters and sampled child memory outside the measurement window. Small state payloads carried an on/off value and sample sequence. The loopback-only harness used an ephemeral test key, multicast TTL zero and destination guards; production port and LAN destinations were rejected. Test tooling and the earlier isolated runs are on the [capacity-test branch](https://github.com/mantiz010/ET-Core/tree/capacity-test-20261108/sim/capacity).

An earlier bounded run using the deployed 0.4.3 sources passed with 2,000 simulated devices. Higher attempts stopped at the host resource guard, so no protocol maximum was established. See the [earlier report](https://github.com/mantiz010/ET-Core/blob/capacity-test-20261108/docs/benchmarks/2026-11-08-capacity/extended/REPORT.md). The headline claim for current main remains **simulator-tested with 1,000 encrypted devices**, not a guarantee of 1,000 physical devices.
