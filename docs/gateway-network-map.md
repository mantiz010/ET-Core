# Gateway nodes in the ET-Core map

The map keeps the existing ET-Core hub/device view and adds a focused branch for recognised ESP-NOW and RF24 gateway profiles. Select a gateway above the map, or open it and choose **Explore gateway map**. Tap a node for its readings, health, matched native HA controls and **Open in HA** entity links.

## What the map represents

- `espnow_uart_v1` / hash `3f1744b4c5fa9629`: 16 identified ESP-NOW/LoRa nodes, plus a separately labelled logical pump-feedback group. The gateway does not supply a source address for that pump feedback, so it is not counted as an additional identified radio node.
- `rf24_network_v1` / hash `46418962e822ff68`: 10 configured nodes, including the switch at octal address `012`. That switch retains its legacy HA entity name; its network node is Node 10.
- Grouping follows the existing versioned channel profiles. This is a logical report path through the gateway, not a Wi-Fi association map or an inferred RF24 routing tree. Remote Wi-Fi IPs, per-node RSSI, physical placement and relay acknowledgements are not invented.

Six cards appear per page on larger screens, three on phones. Each node has a role-specific icon, a last reported value and a last-report age when an actual sensor report has been observed. The all-devices view also shows identified-node counts below the two gateway icons. Grouped nodes do not inflate the count of directly connected ET-Core devices.

## Health semantics

The page combines sparse authenticated gateway state frames using their `age_ms` values and report timestamps. It retains unchanged channels between batches, refuses invalid or unknown keys/ages, ignores older samples, and clears cached reports after gateway reboot or removal. Profile mismatches fail closed. Child freshness uses each profile channel's timeout (currently 1,800 seconds), independently of the gateway's Wi-Fi lease.

- **Reporting:** all expected telemetry channels have recent reports.
- **Partial data:** some telemetry channels are current.
- **No recent data:** observed reports have expired.
- **Not seen:** this view has not yet observed a sensor report for the node. It does not mean the configured hardware has been independently discovered or proved absent.
- **Gateway offline:** the upstream ET-Core gateway is offline; this does not independently diagnose its radio nodes.
- **No device feedback:** command-only RGB nodes and the RF24 switch do not report remote health. A gateway command echo is not used as proof that a physical device is online or actuated.

Last-report ages come from sensor sample ages, not HA `last_changed` timestamps: an unchanged reading can still be fresh. The cache is local to the open page. On reload it rebuilds from the current gateway snapshot and recent authenticated event history; previously stale reports not in that window may show **Not seen** until reported again.

## Home Assistant links and deployment

The page reads HA's entity registry over the existing logged-in frontend connection, matches stable `etcore_<gateway-id>_bridge_<channel>` unique IDs, and restricts links/controls to that gateway's non-disabled entities. Renamed HA entities therefore continue to match. If registry access is unavailable, raw readings still work and the panel explains that entity links are unavailable. No token is embedded in the page and no HA service is called merely by opening or navigating the map.

This update changes the HTML/JavaScript panel only. Live component files and the entity registry were backed up before deployment. Firmware, entities, automations, node timeouts, hub settings and actuator states were not changed by this map update. HA's panel re-reads the HTML when its file changes; refresh the ET-Core page to load it.

## Verification

`node tests/gateway_map/test_model.cjs` runs against the production model embedded in the page. Coverage includes the full 51-channel ESP-NOW and 34-channel RF24 schemas, sparse reports, TTL, gateway outage, command-only feedback, malformed inputs, ordering, reboot reset, profile rejection and removal.

Chromium checks with a private read-only snapshot verified branch navigation, desktop/mobile pagination, keyboard focus retained across polling, native entity links, the feedback-only switch display, mobile width, gateway outage handling and zero service calls/JavaScript errors. Desktop and phone layouts were visually inspected. No real actuator command was issued to test the map.
