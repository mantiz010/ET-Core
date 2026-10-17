# ET-Core simulator

Runs the **real** `ETCore.cpp` device library on Linux (one process per device, each on
its own `127.0.0.x`) against the **real** Home Assistant `hub.py` (Home Assistant itself is
stubbed). Multicast stays on loopback with TTL 0, so nothing reaches the LAN.

    sim/build.sh                                   # builds sim/device_sim
    cd sim && python3 -m unittest -v test_etcore   # protocol tests

Needs: g++, OpenSSL headers (`libssl-dev`), ArduinoJson 7 (default
`~/Arduino/libraries/ArduinoJson/src`, override with `ARDUINOJSON=`).

Scale (loopback): `python3 scale.py 250` (join, steady load, commands, HA restart,
unplug 10).

Hardware (real ESP32 on the LAN, real hub.py on this PC, port 5560; stop any test
Home Assistant first, it uses the same port):

    python3 hw_check.py <psk_file> <ota_pass_file> <fw.bin>      # quick, no unplugging
    python3 hw_soak.py  <psk_file> <ota_pass_file> <fw.bin> 10 30 # 10 OTA reboots + 30 min
    python3 hw_test.py  <psk_file>                               # includes unplug/replug by hand

Home Assistant checks against a test instance: `tools/ha_e2e.py` (switch),
`tools/ha_e2e_light.py` (light), `tools/ha_restart_check.sh <state>` (`ENT=` picks the
entity), `tools/ha_panel_shot.js` (sidebar panel in headless Chromium).
