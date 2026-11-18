#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
L=arduino/libraries
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
C=$L/ETChaCha20Poly1305/src
gcc -O1 -c "$C/rfc8439_chacha20.c" -o "$D/chacha.o"
gcc -O1 -c "$C/poly1305.c" -o "$D/poly.o"
g++ -std=c++17 -O1 -Wall -Wextra -Wno-deprecated-declarations -DARDUINO_ARCH_ESP32 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 -Isim/host -I"$L/ETCore/src" -I"$C" -I"$HOME/Arduino/libraries/ArduinoJson/src" tests/thread_ota/receiver.cpp sim/host/sleep_hooks.cpp "$L/ETCore/src/ETCore.cpp" "$C/ETChaCha20Poly1305.cpp" "$D/chacha.o" "$D/poly.o" -lcrypto -o "$D/receiver"
"$D/receiver"

g++ -std=c++17 -O1 -Wall -Wextra -Wno-deprecated-declarations -DARDUINO_ARCH_ESP32 -Itests/thread_ota/stubs -Isim/host -I"$L/ETCore/src" -I"$C" -I"$HOME/Arduino/libraries/ArduinoJson/src" tests/thread_ota/flash_backend.cpp -lcrypto -o "$D/backend"
"$D/backend"
