# ESP-NOW UART/E220 gateway over encrypted ET-Core

Classic ESP32, default 4 MB OTA partitions, Arduino-ESP32 3.3.11. Preserves the
existing receiver UART (RX16/TX17, 115200) and E220 UART (RX25/TX26, 9600),
node mappings and command syntax. This is the Wi-Fi gateway firmware; the
separate ESP-NOW receiver and its peers are not flashed.

Copy `secrets.h.example` to `secrets.h` and enter your own Wi-Fi, OTA password and
64-hex ET-Core key. Never commit private headers or compiled images.

Requires the native HA `bridge.py` / `bridge_profiles.py` adapter in this branch.
One encrypted gateway represents 51 channels. State snapshots carry sample ages;
boot does not fabricate zero readings. Control echoes are gateway command echoes,
not proof of remote physical actuation. Existing offline nodes remain offline.

The live migration preserved 44 existing HA entity IDs/history. Entity registry
migration is installation-specific: back it up and map by original unique IDs;
do not assume generated friendly-name IDs will be identical in another home.
No physical relay/pump/light commands were issued during deployment validation.
