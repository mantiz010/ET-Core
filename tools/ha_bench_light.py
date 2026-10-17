"""Same checks for an ET-Core or ET-Bus light in the test HA, against the C3.
    bench.py <entity> <udp_port> <effect1> <effect2>"""
import subprocess, sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
E, PORT, FX = sys.argv[1], sys.argv[2], sys.argv[3:5]
C3 = "172.168.1.112"
def log(m): print(f"[{time.strftime('%H:%M:%S')}] {m}", flush=True)
tok = token()
def st():
    s, e = call("GET", f"/api/states/{E}", tok=tok); return e if s == 200 else {}
def wait(pred, timeout, step=0.02):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(step)
    return False
def block(on):
    subprocess.run(["sudo", "-n", "iptables", "-I" if on else "-D", "INPUT", "-s", C3, "-p", "udp", "--dport", PORT, "-j", "DROP"], check=True)
checks = [
    ("turn_on", {}, lambda s: s["state"] == "on"),
    ("turn_on", {"brightness": 80}, lambda s: s["attributes"].get("brightness") == 80),
    ("turn_on", {"rgb_color": [255, 0, 0]}, lambda s: s["attributes"].get("rgb_color") == [255, 0, 0]),
    ("turn_on", {"rgb_color": [0, 0, 255], "brightness": 200}, lambda s: s["attributes"].get("rgb_color") == [0, 0, 255] and s["attributes"].get("brightness") == 200),
    ("turn_on", {"effect": FX[0]}, lambda s: s["attributes"].get("effect") == FX[0]),
    ("turn_on", {"effect": FX[1]}, lambda s: s["attributes"].get("effect") == FX[1]),
    ("turn_off", {}, lambda s: s["state"] == "off"),
    ("turn_on", {}, lambda s: s["state"] == "on"),
    ("turn_off", {}, lambda s: s["state"] == "off"),
    ("turn_on", {"rgb_color": [0, 255, 0], "brightness": 120}, lambda s: s["state"] == "on" and s["attributes"].get("rgb_color") == [0, 255, 0]),
]
lat, bad = [], 0
for _ in range(3):
    for name, data, pred in checks:
        t = time.time()
        call("POST", f"/api/services/light/{name}", {"entity_id": E, **data}, tok=tok)
        if wait(lambda: pred(st()), 5): lat.append((time.time() - t) * 1000)
        else: bad += 1
lat.sort()
log(f"{E}: 30 commands, {len(lat)} ok, {bad} failed, HA call -> device state median {lat[len(lat)//2]:.0f} ms, max {lat[-1]:.0f} ms")
block(True)
try:
    t = time.time()
    ok = wait(lambda: st().get("state") == "unavailable", 120, 0.1)
    log(f"device silent -> unavailable: {ok} after {time.time() - t:.1f} s")
finally:
    block(False)
t = time.time()
ok = wait(lambda: st().get("state") in ("on", "off"), 60, 0.1)
log(f"device back -> available: {ok} after {time.time() - t:.1f} s, state {st().get('state')}")
