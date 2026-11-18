# ET-Core Thread transport development preview

Status: isolated development branch; not installed in Home Assistant or flashed.
This is native ET-Core networking work, not a Matter implementation.

## Implemented foundation

The hub can opt into a separate IPv6-only UDP socket while keeping the existing
IPv4 socket and discovery. Outbound packets select the matching family; source
address selection follows the kernel IPv4/IPv6 route. Encryption, challenges,
command IDs, acknowledgements and replay validation remain in the shared hub.
A failed IPv6 startup closes the IPv4 transport; shutdown closes both families.

Internal developer configuration: `enable_ipv6: true`, optional `ipv6_bind`
(default `::`). These are not yet options exposed in the integration UI.
IPv6 discovery is not implemented: a test peer must know the reachable hub address
and listen on the configured ET-Core port. This does not forward IPv4 multicast
onto Thread. Scoped link-local addresses and interface selection are not yet a
supported commissioning path; plan on routable unicast endpoints.

Nine tests use actual IPv6 loopback datagrams: authenticated state, wrong-key
rejection, replay rejection, outbound socket/source routing, encrypted command
and acknowledgement, concurrent IPv4 traffic, socket shutdown, IPv6 default-off
and startup failure cleanup. The two loopback peers share one IP, so the command
round-trip fixture redirects its destination port to the peer's ephemeral port.
Real devices must listen on the configured ET-Core port. No Thread radio is
involved in these tests.

## Next engineering milestones

1. **SDK transport boundary.** Separate envelope encryption/session/replay logic
   from socket ownership. Existing WiFi/IPv4 remains the default adapter. Supply
   an OpenThread IPv6 UDP adapter with bounded queues, explicit truncation and
   send errors. Generalise retained hub-address storage with a version/migration
   check; never interpret old four-byte RTC data as an IPv6 address.
2. **Thread commissioning and reachability.** Securely provision the operational
   dataset. Verify attachment, routable IPv6 addresses and bidirectional UDP
   through an existing border router. Start with a configured unicast endpoint;
   define authenticated discovery/service registration separately. An installed
   Thread integration is not proof the joining credentials are present.
3. **Payload and sleep policy.** Measure real encrypted datagram sizes and radio
   fragmentation before selecting a Thread payload budget. Tune report, poll,
   retry and lease timing for sleepy endpoints. A sleeping node does not route
   mesh traffic. Do not carry WiFi two-second heartbeat assumptions into a
   battery profile or promise current consumption from a compile.
4. **Transport-aware diagnostics.** Report Thread attachment role, parent/link
   metrics, queue drops and last exchange. Do not substitute WiFi RSSI for
   Thread link quality or label normal sleep as a weak WiFi fault.
5. **Physical C6 validation.** One endpoint using an existing border router is
   enough for first interoperability, subject to securely obtaining its dataset.
   A separate RCP+Linux OTBR test is an alternative, not automatically required.
   Later add a powered router and sleeping endpoint for reparenting tests.
6. **Firmware updates.** Existing ArduinoOTA code uses WiFi; it is not a proven
   Thread update channel. Define bounded transfer/resume, image verification,
   rollback and sleepy-device update windows before claiming Thread OTA support.

Keep the application-layer encryption, IDs and replay protection end-to-end.
The border router should route packets; it should not need ET-Core device keys
or rewrite identity. Network security and application security serve different
boundaries. No claim of Matter interoperability or Thread certification is made.

## Authority

- https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/openthread.html
- https://github.com/espressif/esp-idf/blob/v5.5.1/examples/openthread/ot_br/README.md
- https://openthread.io/guides/border-router
- https://openthread.io/guides/thread-primer/node-roles-and-types
- https://www.home-assistant.io/integrations/thread/

Local network observations and private datasets/keys are intentionally excluded
from this public design document. Research evidence is retained separately.

## Verification — 17 November 2026

The combined simulator suite passed 82 tests in 204.650 seconds. The final
IPv6 suite separately passed nine tests, including the subsequently added
command/acknowledgement round trip. Existing subprocess-file ResourceWarnings
were emitted by the simulator; assertions passed. These results establish host
transport compatibility only, not Thread commissioning, radio operation, packet
fragmentation, sleepy-node behavior or firmware-update delivery over Thread.

## SDK adapter preview

The opt-in `ETCoreTransport` interface now separates datagram socket ownership
from shared envelope/security logic. `ETCoreOpenThread` uses the installed
OpenThread UDP API and an explicitly configured unicast bootstrap. The C6Thread
example supplies a private operational dataset; it never creates an unrelated
Thread network. Default WiFi transport remains available unchanged by default.
The current RTC deep-sleep format remains IPv4-only, so external adapters reject
combining this interface with deep sleep. This avoids silently truncating IPv6
addresses. The preview is not yet a generic third-party transport ABI release.

See [C6Thread prerequisites and limits](arduino/libraries/ETCore/examples/C6Thread/README.md).

SDK preview version is 0.4.9-alpha.1. C6Thread clean-compiled on the exact C6
FQBN above with unchanged sketch sources: 1,212,596 program bytes and 87,248
static RAM bytes. The final adapter regression suite passed 87 tests in
206.550 seconds, including real SDK encrypted commands through an injected
host adapter, visible adapter-start failure, encryption-required attachment
and rejection of the unsupported sleep/adapter combination. This does not
validate Thread radio hardware or claim a battery/OTA-over-Thread implementation.

## First physical Thread run — 18 November 2026

The owner USB-flashed the configured portable sketch onto a spare ESP32-C6.
It attached as a Child to an existing Nest Thread network, with WiFi disabled.
The separate Linux IPv6 test hub accepted the authenticated ET-Core session and
received advancing encrypted counter readings with zero decryption failures.
Three ICMPv6 probes to the node all replied, with observed RTTs 27–54 ms. These
are reachability samples, not a throughput or long-term latency benchmark.
Initial unproven-session packets were challenged before normal traffic was
accepted; they were not counted as wrong-key failures. Production HA was not
changed by this physical run. Actual command acknowledgement, hub outage,
reparenting, sleepy-node behavior and Thread firmware updates remain separate
hardware tests. The portable archive fixes dependency on older installed SDKs.

## Native Home Assistant test — 18 November 2026

An additional encrypted ET-Core config entry now accepts the real C6 counter
over IPv6 through the existing Thread border router. Existing IPv4 entries
remain unchanged; IPv6 is opt-in and requires a key in the config flow.
The device appears as a native HA sensor and is labelled Thread rather than
being given WiFi signal advice. An advancing counter and zero decryption
failures were observed after adoption. This is a test connection, not a claim
of completed Thread OTA, sleepy operation, or hardware command testing.

The already-flashed test node still knows its original bootstrap endpoint. A
private authenticated discovery helper asks HA's admin-only
`etcore/thread_bootstrap` websocket command to issue a normal encrypted peer
challenge from HA's own IPv6 address. The helper does not forward sensor state
or bypass session proof. It is currently required for bootstrap after node resets and for periodic
HA-origin keepalive announcements, because the preview hub multicast announcer
is IPv4-only. Native IPv6 peer announcements, permanent discovery and direct
HA provisioning remain future work. The command is
rate-limited and rejects non-unicast or link-local targets. Never weaken the
device's check that the advertised hub address matches the packet source.

Live dashboard checks passed at phone and tablet widths. Local credentials,
operational datasets and the private helper configuration are not published.

## Direct HA candidate — preview 0.2

The hub now sends encrypted session-bound unicast keepalives to known online,
awake IPv6 peers on the configured announce cadence. IPv4 multicast behavior
is unchanged. The C6 example prints its configured HA endpoint and test counter.
Provision HUB_IPV6 to HA directly to remove the external bootstrap helper after
a USB firmware upload; this is an explicit endpoint, not automatic DNS/SRP
discovery. Offline, unproven, IPv4 and sleeping peers are skipped by the new
periodic unicast sweep.34 focused host regressions passed, including actual
encrypted IPv6 announce contents and periodic sweep invocation. The exact
extended live HA candidate separately passed all 12 IPv6 tests.

Physical keepalive check: the existing C6 remained online for 130 seconds with
the Linux bootstrap helper stopped. Its counter advanced from 273 to 286 and
all sampled decryption-failure counts were zero. This exceeds the SDK's
100-second hub-silence deadline. That proves native keepalive on the tested
path; the installed 0.1 firmware still needs the original helper after a C6
power cycle until the direct-HA 0.2 firmware is USB-uploaded.

With the helper still stopped, reloading only the HA Thread test entry
re-established authenticated readings and advancing counters in 15.06 seconds
with zero decryption failures. No main entry or production device was reloaded
for this recovery check. This is a hub-instance restart test, not a radio
reparenting or C6 power-cycle test.
