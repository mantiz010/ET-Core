"""ET-Core light entity checks in the throwaway HA against the C3 running LightTest."""
import sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
E = "light.et_core_light_test"
def log(m): print(f"[{time.strftime('%H:%M:%S')}] {m}", flush=True)
tok = token()
def st():
    s, e = call("GET", f"/api/states/{E}", tok=tok); return e if s == 200 else {}
def wait(pred, timeout=3, step=0.02):
    end = time.time() + timeout
    while time.time() < end:
        if pred(): return True
        time.sleep(step)
    return pred()
def svc(name, **data):
    t = time.time()
    s, _ = call("POST", f"/api/services/light/{name}", {"entity_id": E, **data}, tok=tok)
    return s, t
end = time.time() + 30
while not st() and time.time() < end: time.sleep(0.5)
a = st().get("attributes", {})
log(f"entity: {st().get('state')} name={a.get('friendly_name')!r} effects={a.get('effect_list')} modes={a.get('supported_color_modes')} features={a.get('supported_features')}")
ok = bad = 0
checks = [
    ("turn_on", {}, lambda s: s["state"] == "on"),
    ("turn_on", {"brightness": 80}, lambda s: s["attributes"].get("brightness") == 80),
    ("turn_on", {"rgb_color": [255, 0, 0]}, lambda s: s["attributes"].get("rgb_color") == [255, 0, 0]),
    ("turn_on", {"rgb_color": [0, 0, 255], "brightness": 200}, lambda s: s["attributes"].get("rgb_color") == [0, 0, 255] and s["attributes"].get("brightness") == 200),
    ("turn_on", {"effect": "blink"}, lambda s: s["attributes"].get("effect") == "blink"),
    ("turn_on", {"effect": "breathe"}, lambda s: s["attributes"].get("effect") == "breathe"),
    ("turn_on", {"effect": "none"}, lambda s: s["attributes"].get("effect") == "none"),
    ("turn_off", {}, lambda s: s["state"] == "off"),
    ("turn_on", {}, lambda s: s["state"] == "on" and s["attributes"].get("rgb_color") == [0, 0, 255]),
    ("turn_off", {}, lambda s: s["state"] == "off"),
]
for name, data, pred in checks:
    s, t = svc(name, **data)
    good = wait(lambda: bool(st()) and pred(st()))
    ms = (time.time() - t) * 1000
    ok += good; bad += not good
    log(f"{'ok ' if good else 'BAD'} {name} {data} -> {st().get('state')} {ms:.0f} ms" + ("" if good else f" attrs={st().get('attributes')}"))
log(f"light checks: {ok} ok, {bad} bad")
