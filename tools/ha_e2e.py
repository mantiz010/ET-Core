"""End-to-end ET-Core checks in the throwaway Home Assistant with the real ESP32-C3."""
import subprocess, sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
E = "switch.hwtest1"; C3 = "172.168.1.112"
def log(m): print(f"[{time.strftime('%H:%M:%S')}] {m}", flush=True)
def st(tok):
    s, e = call("GET", f"/api/states/{E}", tok=tok); return e if s == 200 else {}
def wait(pred, timeout, step=0.1):
    end = time.time() + timeout
    while time.time() < end:
        if pred(): return True
        time.sleep(step)
    return pred()
def block(on):
    act = "-I" if on else "-D"
    subprocess.run(["sudo", "-n", "iptables", act, "INPUT", "-s", C3, "-p", "udp", "--dport", "5560", "-j", "DROP"], check=True)
tok = token()
log(f"entity: {st(tok).get('state')} name={st(tok).get('attributes', {}).get('friendly_name')!r}")
# Toggle 10 times; state must come back from the device
lat, bad = [], 0
for i in range(10):
    want = "on" if i % 2 == 0 else "off"
    t = time.time()
    call("POST", f"/api/services/switch/turn_{want}", {"entity_id": E}, tok=tok)
    if wait(lambda: st(tok).get("state") == want, 3, 0.02): lat.append((time.time() - t) * 1000)
    else: bad += 1
log(f"toggle x10: {len(lat)} ok, {bad} failed, HA service->state median {sorted(lat)[len(lat)//2]:.0f} ms max {max(lat):.0f} ms" if lat else f"toggle failed {bad}")
# Device disappears (traffic blocked on this PC), then comes back
block(True)
try:
    t = time.time()
    ok = wait(lambda: st(tok).get("state") == "unavailable", 20, 0.1)
    log(f"device silent -> unavailable: {ok} after {time.time() - t:.1f} s")
finally:
    block(False)
t = time.time()
ok = wait(lambda: st(tok).get("state") in ("on", "off"), 20, 0.1)
log(f"device back -> available: {ok} after {time.time() - t:.1f} s, state {st(tok).get('state')}")
