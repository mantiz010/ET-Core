# Media-player verification

- `sh sim/run-tests.sh`: real C++ ETCore transport and Python hub on a verified
  unused loopback UDP port. Includes native media commands/state, encryption,
  retries, replay rejection, lease expiry and restart recovery. No live LAN
  devices are used.
- `g++ -std=c++17 -Wall -Wextra -I/path/to/ArduinoJson/src tests/media_protocol_test.cpp -o /tmp/media-protocol-test && /tmp/media-protocol-test`:
  compiles and tests the exact command parser used by the firmware example.
- `python tests/test_media_player.py -v`: run in a Python environment with the
  installed Home Assistant dependencies. It imports the real HA media APIs and
  candidate hub; only the device transport is fake. No HA setup, service call,
  socket, broker or device action is started. `ETCORE_COMPONENT` can point to a
  staged component directory without loading its `__init__.py`.

Firmware acceptance additionally requires an exact WROVER/PSRAM/OTA-layout
build with pinned Audio/ArduinoJson/ETCore dependencies. Do not substitute an
arbitrary global Arduino audio-library installation. The migration and OTA
hardware acceptance gates remain in `docs/media-player.md`.
