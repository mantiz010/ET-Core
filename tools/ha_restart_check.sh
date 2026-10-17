#!/bin/bash
# Restart the throwaway HA and time how long until switch.hwtest1 shows the device's real state.
cd ~/ha-test
P=$(ps -eo pid,args | awk '/[v]env\/bin\/python .*hass -c/{print $1}')
[ -n "$P" ] && kill $P && while ps -p $P >/dev/null 2>&1; do sleep 1; done
nohup ./venv/bin/hass -c ~/ha-test/config > ~/ha-test/hass.out 2>&1 &
until curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8123/api/ 2>/dev/null | grep -q 401; do sleep 0.5; done
T0=$(date +%s.%N)
./venv/bin/python - "$T0" "$1" <<'PY'
import sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
t0, want = float(sys.argv[1]), sys.argv[2]
tok = token(); end = time.time() + 90; seen = []
while time.time() < end:
    s, e = call("GET", "/api/states/" + (__import__("os").environ.get("ENT") or "switch.hwtest1"), tok=tok)
    v = e.get("state") if s == 200 else f"http{s}"
    if not seen or seen[-1][1] != v: seen.append((round(time.time() - t0, 1), v))
    if v == want: break
    time.sleep(0.2)
print("after HA API up:", seen)
s, e = call("GET", "/api/states/" + (__import__("os").environ.get("ENT") or "switch.hwtest1"), tok=tok); print("name:", e.get("attributes", {}).get("friendly_name"))
PY
