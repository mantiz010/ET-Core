# ET-Core simulator

Runs the **real** `ETCore.cpp` device library on Linux (one process per device, each on
its own `127.0.0.x`) against the **real** Home Assistant `hub.py` (Home Assistant itself is
stubbed). Multicast stays on loopback with TTL 0, so nothing reaches the LAN.

    sim/build.sh                                   # builds sim/device_sim
    cd sim && python3 -m unittest -v test_etcore   # protocol tests

Needs: g++, OpenSSL headers (`libssl-dev`), ArduinoJson 7 (default
`~/Arduino/libraries/ArduinoJson/src`, override with `ARDUINOJSON=`).
