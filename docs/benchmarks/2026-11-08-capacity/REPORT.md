# ET-Core encrypted capacity ramp — 8 November 2026

All five isolated simulator stages passed. These results measure the protocol hub and portable host simulation, not Wi-Fi capacity or a full HA instance.

| Devices | Packets/s | Hub/harness CPU, one core | Missing state reports | Command ACK p95 | Hub recovery |
|---:|---:|---:|---:|---:|---:|
| 25 | 15.0 | 0.68% | 0/75 | 3.57 ms | 1.88 s |
| 50 | 30.0 | 0.96% | 0/150 | 3.41 ms | 2.03 s |
| 100 | 60.0 | 1.52% | 0/300 | 3.36 ms | 2.05 s |
| 150 | 90.0 | 2.06% | 0/450 | 3.32 ms | 2.05 s |
| 250 | 150.0 | 3.61% | 0/750 | 3.37 ms | 2.02 s |

Across the ramp: 1725 periodic state reports, none missing; 225 virtual commands, none failed; 50/50 simulated unplugs detected; no false offline events. All virtual device processes were cleaned up.

## Method and isolation

Each level had 30 seconds of steady state after join/settle. Every virtual device had a unique loopback address and encryption identity, a 2-second lease, 10-second TTL and one small encrypted state report every 10 seconds. Each stage also exercised up to 50 virtual command ACKs, a simulated hub-object restart, and ten abrupt simulated unplugs. State report sequence counters were checked after a 250 ms drain; missing-report counts do not claim zero loss of every UDP datagram. The reported decrypt/replay counters in raw results are from the post-restart hub.

The actual deployed ETCore.cpp / crypto library and matching hub.py were frozen and hashed. The simulator compiles the common library through its portable UDP host shim; it does not exercise ESP-IDF sockets or ESP32 CPU/heap. Home Assistant core objects, persistent counter storage, entities and recorder are stubbed/absent. No live HA API was contacted, no real hub was restarted and no real device was commanded. Port 18560, multicast TTL zero and loopback routing were enforced in the wrappers; four forbidden destination checks passed. Test keys were random, ephemeral and unrelated to production.

The benchmark ran on a shared Linux host with 32 logical CPUs, at nice level 10. CPU is normalized to one core and includes Python recording/monitoring overhead. The hub/harness process RSS was about 40 MiB at 250 devices; device-process RSS is reported separately and summed RSS includes shared pages. Each measurement window was only 30 seconds, so this is not a long soak test. See environment/source manifests for runtime and source hashes.

## Interpretation

The protocol/hub test handled 250 virtual devices at this workload on this host. It is not proof that the production HA VM, recorder or access point supports 250 physical Wi-Fi devices, nor proof that 250 is a limit. More frequent/larger bridge updates and real Wi-Fi airtime need separate tests. No production configuration was changed.
