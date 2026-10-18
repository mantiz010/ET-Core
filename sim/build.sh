#!/bin/sh
# Builds sim/device_sim from the real ETCore library sources (host shim in sim/host).
set -e
cd "$(dirname "$0")"
L=../arduino/libraries
AJ=${ARDUINOJSON:-$HOME/Arduino/libraries/ArduinoJson/src}
CH=$L/ETChaCha20Poly1305/src
gcc -O1 -c $CH/rfc8439_chacha20.c -o /tmp/etcore_sim_chacha.o
gcc -O1 -c $CH/poly1305.c -o /tmp/etcore_sim_poly.o
g++ -std=c++17 -O1 -Wall -Wno-deprecated-declarations \
  -DETCORE_DEFAULT_PORT=${ETCORE_SIM_PORT:-15560} ${ETCORE_SIM_CFLAGS:-} -DARDUINO_ARCH_ESP32 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 \
  -Ihost -I$L/ETCore/src -I$CH -I"$AJ" \
  device_sim.cpp $L/ETCore/src/ETCore.cpp $CH/ETChaCha20Poly1305.cpp \
  /tmp/etcore_sim_chacha.o /tmp/etcore_sim_poly.o -lcrypto -o device_sim
echo built sim/device_sim
