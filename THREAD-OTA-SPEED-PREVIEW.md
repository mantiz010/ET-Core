# Thread OTA speed candidate — SDK 0.4.9-alpha.3

The accepted baseline transferred a 1,222,944-byte C6 application in 862.347s,
about 1,418 application bytes/s. Command round trips averaged about 133ms. That
baseline is physical evidence. A subsequent physical test of the new profile
failed before activation; it is not a successful speed improvement.

## Changes

- New receivers advertise a maximum 288-byte chunk and a two-chunk window.
- After the physical failure below, the Git sender defaults to 192-byte chunks /
  window 1 even when a receiver advertises 288 / 2. Windowed selection is disabled
  by default; its implementation remains available only for controlled tests.
  Existing 192-byte receivers keep the original serial behavior.
- A new receiver still accepts an unextended begin as 192 bytes/window 1. For
  explicit negotiation, only reviewed chunk sizes 192/288 and windows 1/2 are
  accepted. An old HA sender with an exact 192-byte capability check must be
  upgraded before managing the new advertisement; it rejects unsupported
  capability metadata rather than sending unreviewed data.
- Two bounded slots accept out-of-order chunks. Flash writes remain sequential.
  Conflicting duplicates, offsets outside the receive window, unsupported
  negotiation and flash-write failures are rejected. Recent written chunks are
  retained for duplicate verification.
- A positive chunk ACK can acknowledge buffered acceptance. The sender settles
  the entire window before advancing; an earlier negative ACK cannot be hidden
  by a later positive one. The finish barrier still requires every byte written
  and a full SHA/native image verification before boot selection.
- Command encryption, per-boot addressing, sequence/replay protection, safe
  retransmission, admin review/confirmation, inactive-slot writes and health/
  rollback gates remain in place. Cancelled waits remove pending retry timers.

The window is deliberately bounded at two while the installed OpenThread UDP
receive queue has four entries. New firmware uses 88,800 static RAM bytes versus
87,792 in the accepted baseline: a 1,008-byte increase. No PSRAM or external GPIO
is added. Both candidate applications clean-compiled unchanged for
`esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=default` with
Arduino-ESP32 3.3.11, using 1,224,292 program bytes.

## Evidence and boundaries

The real shared C++ SDK/receiver tests exercised encrypted reordered commands,
a stale-sequence reply followed by a fresh-sequence resend, lost-ACK duplicate
handling, ordered writes and a second write failing after a buffered positive
ACK. Activation remained blocked on failure. Legacy serial transfers and bounded
reorder-range/conflicting-duplicate checks passed too.

The HA tests verify legacy fallback, no more than two commands outstanding,
reversed acknowledgement timing, and settling a failed window before abort.
With injected 12–16ms acknowledgement latency and a synthetic 16KB application,
observed local runs were approximately 2.0–2.4 times faster. Those figures are
host-test results only; actual Thread fragmentation, radio scheduling and flash
latency can change or eliminate that gain. Largest supported 64-character IDs
and 288-byte encrypted command packets passed the 1024-byte datagram limit test.

## Physical tests — 19 November 2026

Both tests were explicitly authorised and used the same ESP32-C6, direct encrypted
Thread link, board target and 1,224,448-byte application size. Only the HA OTA
coordinator was upgraded, after a configuration backup and HA configuration check.

| Transfer | Profile | Result | Measured duration |
| --- | --- | --- | --- |
| 0.4 → 0.5 | 192 bytes / window 1 | Succeeded, new authenticated boot and health ACK | 893.908s (14m 54s), 1,369.8 application bytes/s |
| 0.5 → 0.6 | 288 bytes / window 2 | Failed at 694,080 acknowledged bytes (56%); no activation | 778.679s until failure; not a completed-image speed result |

The serial pass recorded zero failed commands. The windowed pass accumulated 147
hub command failures, while decryption failures stayed at zero. Captured events
show positive ACKs arriving after the hub's three-attempt, approximately 0.9-second
retry horizon. Retransmissions and late replies are an observed problem; this
capture alone does not isolate every underlying radio or scheduling cause.
Ultimately a command exceeded the OTA coordinator's eight-second wait. Its empty
exception string produced a blank UI error; the Git candidate now reports the
exception type when no message is present, with a regression test.

The failed transfer sent abort and never sent commit. The device remained online
on 0.5 in the same authenticated session, with its test counter advancing after
failure. Home Assistant was RUNNING, all eight production devices were online,
and all 232 existing ET-Core entity IDs were unchanged. No third update was
attempted. A successful 0.6 boot, physical power-cut and rollback fault tests have
**not** been demonstrated.

The 288/window2 mode is therefore **not recommended** as a speed improvement.
The Git sender now selects the proven serial profile by default. This safety
change is tested in source; the live sender used for the measurement has not been
reloaded with it, and no further live OTA is running. Timing and congestion policy
need further work before another explicitly authorised hardware test.

Configured source, firmware images, keys, addresses and raw snapshots stay private.
Only this generic measured summary is published.

## Verification

Before the physical tests, 125 broad regressions passed in 207.128s, and the final
focused IPv6 packet-budget suite passed 14 tests. Following the failure, all 21
OTA coordinator tests passed, including serial fallback on a fast-capable receiver
and a visible timeout error without activation. The injected-latency host model
still shows about 2.37x; the physical failure demonstrates why that is **not** a
radio performance claim.
