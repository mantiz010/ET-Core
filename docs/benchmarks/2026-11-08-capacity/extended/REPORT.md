# Extended capacity search — 8 November 2026

**Highest fully verified level: 2,000 virtual encrypted devices. A true protocol
maximum was not found.** Higher-count startup attempts were stopped by the test
host's load safety guard, not an identified fixed device cap in ET-Core.

| Scenario | Target | Result |
|---|---:|---|
| Raw collector, 60-second steady window | 500 | Pass, 3000 generated state reports, none missing |
| Raw collector, 60-second steady window | 1000 | Pass, 6001 reports, none missing |
| Raw collector, 60-second steady window | 2000 | 86 of 12080 reports missing; not CPU-saturated |
| Bounded collector, same protocol/buffers | 2000 | Pass, 12023 reports, none missing; zero kernel socket drops |
| Bounded collector | 4000 target | Host-load guard aborted while starting processes; no valid full measurement |
| Bounded collector, staggered process starts | 3000 target | Host-load guard aborted during startup; no valid full measurement |

## Clean 2000-device result

- Leases: every 2 seconds; TTL: 10 seconds; one small encrypted state every 10 seconds.
- Steady measurement: 60 seconds, about 1200.6 incoming packets/s.
- Hub/monitor CPU: 37.55% of one core on this shared 32-thread host.
- 12023 reports generated, none missing; zero false offline events.
- 50/50 virtual commands acknowledged; ACK p95 5.34 ms, max 5.47 ms.
- All devices rejoined with names/state after simulated hub replacement in 12.42 s.
- 10/10 simulated unplug events detected in 8.65–9.66 s, no other false offlines.
- Kernel receive buffer stayed at its default 212992 bytes; no sysctl or socket
  receive-buffer tuning was applied. The reported per-socket drop count was zero.

## Observer effects and limitations

The first instrumented 2000-device attempt retained all raw packets and read child
process memory synchronously on the receiver loop. For the diagnostic rerun,
packet/event collection was bounded, child RSS sampling moved outside the measured
window, publication-counter scans yielded to the receiver and counted reports
were drained before comparison. The original failing result is retained above.
Join/recovery counting was also corrected to avoid copying the full device table
once per device. None of these changes altered production protocol/firmware code.

Higher attempts encountered the selected host-load guard (one-minute load above
85% of logical CPU count). This is a conservative stopping policy, not evidence
that the computer has a hard 3000-device limit or that ET-Core cannot handle more.
The one-process-per-device generator and multicast startup work also load the host.
Staggering joins did not complete the 3000 target under that guard in this run.
No successful 3000- or 4000-device capacity result is claimed.

All runs used the isolated port and loopback-only wrappers described in the base
report. Production HA, access points, device firmware and configuration were not
changed. Every test device process was cleaned up. This result does not establish
Wi-Fi station capacity, ESP32 CPU/heap limits, recorder/entity-layer capacity or a
production HA VM maximum. The one-minute steady windows are not long soak tests.
Larger/faster bridge messages require a separate workload, and the virtual
command latency excludes real Wi-Fi and actuator response time.

The raw JSON includes receiver-side and simulator RSS; summed child RSS contains
shared pages and is not unique physical RAM consumption. Hub CPU/RSS include
monitoring and allocator overhead. See the base environment/source manifests.
