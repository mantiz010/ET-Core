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
and all 232 existing ET-Core entity IDs were unchanged. No additional update was attempted during that experiment. A successful 0.6 boot
was not demonstrated by the windowed test; the later serial recovery is recorded
below. Physical power-cut and rollback fault tests remain **not demonstrated**.

The 288/window2 mode is therefore **not recommended** as a speed improvement.
The Git sender now selects the proven serial profile by default. At the end of that experiment this safety change was source-tested only. The
subsequent explicitly authorised serial recovery below deployed it together with
the timing repair. Windowed timing and congestion policy still need further work
before another explicitly authorised hardware test.

Configured source, firmware images, keys, addresses and raw snapshots stay private.
Only this generic measured summary is published.

## Verification

Before the physical tests, 125 broad regressions passed in 207.128s, and the final
focused IPv6 packet-budget suite passed 14 tests. Following the failure, all 21
OTA coordinator tests passed, including serial fallback on a fast-capable receiver
and a visible timeout error without activation. The injected-latency host model
still shows about 2.37x; the physical failure demonstrates why that is **not** a
radio performance claim.

## Timing repair after the failed test

The source now uses a 1.5-second retry interval specifically for `firmware.ota`
commands to authenticated Thread devices. Other commands retain their 300ms
interval. The attempt limit remains three, so a missing reply exhausts retries
in 4.5 seconds, inside the coordinator's eight-second wait. Command IDs remain
stable across retries and each resend uses a fresh sequence. No packet format,
firmware receiver, encryption or activation policy changes.

Deterministic tests run the real hub retry state machine with one-second ACKs,
a lost initial reply, complete reply loss, wrong-device ACKs and unchanged normal
command timing. IPv6 loopback and OTA coordinator/view tests passed too (48 total).
These were source-test results before deployment. The later live serial check is
recorded below; the windowed preview remains disabled by default.

The full isolated simulator regression passed **134 tests in 207.851s**, including
the timing repair, encrypted command delivery, duplicate suppression, sleeping
node recovery and IPv6. No live hardware was changed during this repair.

## Deployed serial recovery — 19 November 2026

After explicit approval, the live Home Assistant component and storage were backed
up. Only the reviewed retry-timing delta was applied to the existing live hub;
private production features were retained. The coordinator's serial fallback and
visible timeout error were installed too. Python syntax and Home Assistant's
configuration check passed before a single restart. All eight production devices
and all 232 entity IDs were checked before installation.

The same previously clean-compiled 1,224,448-byte 0.6 application was then installed
on the spare C6 using **192-byte chunks / window 1**. It succeeded in **886.422s
(14m 46s), 1,381.3 application bytes/s**, with zero failed commands and zero
decryption failures. The device booted into a new authenticated 0.6 session,
passed its health ACK and remained online beyond **190 seconds uptime**, with its
test counter advancing to 19. The final check found Home Assistant RUNNING, all
eight production devices online and all 232 prior ET-Core entity IDs unchanged.

This validates a completed serial OTA under the repaired timing policy. It does
**not** establish a windowed speed improvement, isolate the timing change from the
serial-profile change, or prove physical power-cut/rollback fault recovery. The
failed windowed result remains above. No production device firmware was changed.

## Interrupted-update regression coverage

A subsequent read-only check found the live 0.6 C6 online after more than ten
hours, with its test counter advancing, Home Assistant RUNNING and all eight
production devices online. No device was flashed or restarted for these tests.

The host build of the actual C++ receiver now explicitly exercises timeout and
abort with an out-of-order chunk buffered but not yet written. Both discard that
transfer; old chunks, finish and commit are rejected. A fresh transfer with a new
ID then writes and verifies its own image without leftover bytes. A fully
verified image that expires before commit also cannot select a boot slot.

The HA coordinator tests cover a receiver reboot during a multi-chunk transfer,
cancellation removing pending retry timers, interruption after commit without a
false success or an unsafe abort claim, and restoring every active job phase
without automatically sending commands. All **32 coordinator/view tests** passed;
the C++ receiver and native flash-backend host suites passed too. These are
regression tests, not physical power-cut or rollback fault tests. Firmware and
live Home Assistant source were unchanged by this test-only work.

## New interruption trial — 20 November 2026 (incomplete)

A version-label-only 0.7 application clean-compiled for the same exact C6 target,
with unchanged source hashes through compilation (1,224,448-byte BIN). The plan
was to stop at 50%, verify the old boot, then retry. The actual serial transfer
instead hit an eight-second acknowledgement timeout at **16,320 bytes (1%)**,
before the planned interruption. The hub exhausted three attempts; decryption
failures stayed at zero. The receiver positively acknowledged abort, and no
commit or connection reload was issued.

A subsequent check confirmed 0.6 still online in its original session with its
counter advancing, Home Assistant RUNNING, all eight production devices online
and all 232 prior ET-Core entity IDs preserved. This demonstrates safe abort of
that real failed transfer, **not** a completed halfway-interruption/reboot/retry
test. A physical reset of the spare is needed to verify the old boot because
0.6 has no remote reset command. The retry has not yet been performed. This new
failure also means the earlier successful serial run must not be treated as a
guarantee of reliable delivery under every radio condition. The cause of the
missing chunk replies is not established by this capture alone.
