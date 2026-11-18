# Thread OTA speed candidate — SDK 0.4.9-alpha.3

The accepted baseline transferred a 1,222,944-byte C6 application in 862.347s,
about 1,418 application bytes/s. Command round trips averaged about 133ms. That
baseline is physical evidence; the new speed candidate below is not yet a
Thread radio benchmark.

## Changes

- New receivers advertise a maximum 288-byte chunk and a two-chunk window.
- The HA sender negotiates the supported profile. Existing 192-byte receivers
  without a window advertisement keep the original serial behavior.
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

## Proposed physical test

The live C6 remains on verified 0.4; no live firmware or server code was changed
while preparing this candidate. The staged receiver is 0.5, followed by an exact
separate 0.6 application image for measuring the new transfer profile.

1. Back up and install the compatible HA sender, retaining the existing entry,
   keys and entity IDs. Current 0.4 still uses 192/window 1.
2. Explicitly authorised OTA 0.4→0.5 installs the new receiver using that existing
   serial profile. This first pass is not the speed benchmark.
3. Verify 0.5, its new session, health ACK, chunk/window advertisement and normal
   readings. Then an explicitly authorised OTA 0.5→0.6 can measure the new mode.
4. Compare application bytes per second, verification/boot time, command failures
   and reading continuity with the accepted baseline. Do not claim a gain from
   host timings, or claim hardware rollback/power-cut tests have been performed.

Both updates can use the existing OTA path; no USB reflash is needed for this
plan. Configured source, images, keys, addresses and raw snapshots stay private.

Verification for this candidate: 125 broad regressions passed in 207.128s.
The final focused IPv6 packet-budget suite passed 14 tests, including the added
maximum-ID case. The full-run injected-latency comparison was 1.315s serial
versus 0.558s windowed (2.36x), again a host-model result. No live C6 or HA
server update has been performed while preparing these files.
