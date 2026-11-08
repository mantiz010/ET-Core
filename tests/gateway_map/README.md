# Gateway map tests

Run `node tests/gateway_map/test_model.cjs` from the repository root. The test extracts the production child-map model from the panel HTML and checks full channel coverage, profile hashes, sparse reports, report age/TTL, gateway outages, feedback-only controls, malformed data, out-of-order frames, reboot resets and removal. It does not contact hardware or call HA services.

The deployed page was also checked in Chromium with a private read-only HA snapshot: branch navigation, desktop/mobile pagination, stable keyboard focus across polling, exact entity links, a feedback-only switch, gateway outage display, zero service calls and no JavaScript errors. Private HA snapshots and browser credentials are not included in Git.
