#!/bin/sh
# Use an unused port; never collide with a local HA test instance or live devices.
set -eu
cd "$(dirname "$0")/.."
ETCORE_SIM_PORT=$(python3 - "${ETCORE_SIM_PORT:-0}" <<'PY'
import socket, sys
port=int(sys.argv[1])
if port and not 1024 <= port <= 65535:raise SystemExit('Choose an unprivileged simulator port')
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
try:s.bind(('',port))
except OSError:raise SystemExit('Simulator UDP port is already in use; choose another port')
print(s.getsockname()[1]);s.close()
PY
)
export ETCORE_SIM_PORT
printf 'Isolated simulator UDP port: %s\n' "$ETCORE_SIM_PORT"
sh sim/build.sh
PYTHONPATH="$(pwd)/sim${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest -v sim/test_etcore.py
