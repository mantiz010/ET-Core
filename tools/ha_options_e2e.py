"""Options flow (bad key rejected, valid change reloads) and etcore.send_command, test HA + C3 (PT RGB)."""
import sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
tok = token()
L = "light.pt_rgb"
def st(e): return call("GET", f"/api/states/{e}", tok=tok)[1]
def wait(pred, timeout=20, step=0.1):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(step)
    return False
entries = call("GET", "/api/config/config_entries/entry", tok=tok)[1]
eid = next(e["entry_id"] for e in entries if e["domain"] == "etcore")
psk = open("/home/mantiz010/et-core-hwtest/test_psk.txt").read().strip()
s, flow = call("POST", "/api/config/config_entries/options/flow", {"handler": eid}, tok=tok)
print("form fields:", [f["name"] for f in flow.get("data_schema", [])])
s, r = call("POST", f"/api/config/config_entries/options/flow/{flow['flow_id']}", {"psk_hex": "abc", "port": 5560}, tok=tok)
print("bad key ->", r.get("type"), r.get("errors"))
s, r = call("POST", f"/api/config/config_entries/options/flow/{flow['flow_id']}", {"psk_hex": psk.upper(), "port": 5560}, tok=tok)
print("valid ->", r.get("type"))
t = time.time()
wait(lambda: st(L)["state"] == "unavailable", 5)
back = wait(lambda: st(L)["state"] in ("on", "off"), 30)
print(f"after reload: light back {back} in {time.time() - t:.1f} s")
s, r = call("POST", "/api/services/etcore/send_command", {"device_id": "hwrgb1", "payload": {"on": True, "effect": "pulse", "r": 0, "g": 255, "b": 255}}, tok=tok)
print("send_command ->", s)
print("device confirmed:", wait(lambda: st(L)["state"] == "on" and st(L)["attributes"].get("effect") == "pulse", 5))
s, r = call("POST", "/api/services/etcore/send_command", {"device_id": "nosuchdevice", "payload": {"on": True}}, tok=tok)
print("unknown device ->", s, str(r)[:120])
