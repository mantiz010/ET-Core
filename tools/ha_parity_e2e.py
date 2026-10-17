"""ET-Bus parity checks in the test HA against the C3 running ParityTest type N.
    pt_e2e.py <type>"""
import sys, time
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
T = int(sys.argv[1])
NAME = {1: "PT Multi", 2: "PT Fan", 3: "PT Fan Preset", 4: "PT Air", 5: "PT Temp", 6: "PT RGB"}[T]
tok = token()
ok = bad = 0
def states():
    return [s for s in call("GET", "/api/states", tok=tok)[1] if str(s["attributes"].get("friendly_name", "")).startswith(NAME)]
def st(e):
    return call("GET", f"/api/states/{e}", tok=tok)[1]
def wait(pred, timeout=5, step=0.05):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(step)
    return False
def check(label, cond):
    global ok, bad
    ok += bool(cond); bad += not cond
    print(f"  {'ok ' if cond else 'BAD'} {label}", flush=True)
def svc(domain, service, data):
    s, r = call("POST", f"/api/services/{domain}/{service}", data, tok=tok)
    if s != 200: print(f"    service {domain}.{service} -> {s} {str(r)[:200]}")
end = time.time() + 40
while time.time() < end and not any(s["state"] not in ("unavailable", "unknown") for s in states()):
    time.sleep(0.5)
for s in states():
    a = s["attributes"]
    print(f"{s['entity_id']}: {s['state']} {a.get('friendly_name')!r} unit={a.get('unit_of_measurement')} class={a.get('device_class')}")
E = {s["entity_id"]: s for s in states()}
if T == 1:
    sws = sorted(e for e in E if e.startswith("switch."))
    check(f"3 switch entities named Pump/Lamp/Heater: {sws}", len(sws) == 3 and all(any(n in e for e in sws) for n in ("pump", "lamp", "heater")))
    for e in sws:
        for want in ("on", "off"):
            svc("switch", f"turn_{want}", {"entity_id": e})
            others = [o for o in sws if o != e]
            before = {o: st(o)["state"] for o in others}
            check(f"{e} {want}", wait(lambda: st(e)["state"] == want))
            check(f"  others unchanged", all(st(o)["state"] == before[o] for o in others))
elif T == 2:
    f = next(e for e in E if e.startswith("fan."))
    svc("fan", "set_percentage", {"entity_id": f, "percentage": 40})
    check("set 40%", wait(lambda: st(f)["attributes"].get("percentage") == 40 and st(f)["state"] == "on"))
    svc("fan", "turn_off", {"entity_id": f})
    check("turn_off", wait(lambda: st(f)["state"] == "off"))
    svc("fan", "turn_on", {"entity_id": f})
    check("turn_on", wait(lambda: st(f)["state"] == "on"))
    svc("fan", "turn_on", {"entity_id": f, "percentage": 75})
    check("turn_on 75%", wait(lambda: st(f)["attributes"].get("percentage") == 75))
elif T == 3:
    f = next(e for e in E if e.startswith("fan."))
    check(f"presets from hello: {st(f)['attributes'].get('preset_modes')}", st(f)["attributes"].get("preset_modes") == ["off", "low", "medium", "high", "turbo"])
    svc("fan", "set_preset_mode", {"entity_id": f, "preset_mode": "turbo"})
    check("preset turbo", wait(lambda: st(f)["attributes"].get("preset_mode") == "turbo" and st(f)["state"] == "on"))
    svc("fan", "turn_off", {"entity_id": f})
    check("turn_off", wait(lambda: st(f)["state"] == "off"))
    svc("fan", "turn_on", {"entity_id": f})
    check("turn_on", wait(lambda: st(f)["state"] == "on"))
elif T == 4:
    want = {"co2": ("612", "ppm", "carbon_dioxide"), "temperature": ("21.5", "°C", "temperature"), "humidity": ("48.2", "%", "humidity")}
    for k, (v, u, c) in want.items():
        e = next((x for x in E if x.startswith("sensor.") and k in x), None)
        a = st(e)["attributes"] if e else {}
        check(f"{k}: {e} = {st(e)['state'] if e else None} {a.get('unit_of_measurement')} {a.get('device_class')} {a.get('state_class')}",
              e and st(e)["state"] == v and a.get("unit_of_measurement") == u and a.get("device_class") == c and a.get("state_class") == "measurement")
    check(f"exactly 3 sensors: {sorted(E)}", len([e for e in E if e.startswith('sensor.')]) == 3)
elif T == 5:
    e = next((x for x in E if x.startswith("sensor.")), None)
    a = st(e)["attributes"] if e else {}
    check(f"value style: {e} = {st(e)['state'] if e else None} {a.get('unit_of_measurement')} {a.get('device_class')}",
          e and st(e)["state"] == "19.25" and a.get("unit_of_measurement") == "°C" and a.get("device_class") == "temperature")
elif T == 6:
    l = next(e for e in E if e.startswith("light."))
    n = next((e for e in E if e.startswith("number.")), None)
    fx = st(l)["attributes"].get("effect_list") or []
    check(f"35 ET-Bus default effects without a hello list: {len(fx)}", len(fx) == 35 and "rainbow" in fx)
    svc("light", "turn_on", {"entity_id": l, "effect": "rainbow", "rgb_color": [255, 0, 128]})
    check("effect rainbow + colour", wait(lambda: st(l)["attributes"].get("effect") == "rainbow" and st(l)["attributes"].get("rgb_color") == [255, 0, 128]))
    check(f"speed slider exists: {n}", n is not None)
    if n:
        svc("number", "set_value", {"entity_id": n, "value": 200})
        check("speed 200 confirmed by device", wait(lambda: float(st(n)["state"]) == 200 and st(l)["attributes"].get("speed") == 200))
    svc("light", "turn_on", {"entity_id": l, "effect": "fire"})
    check("other default effect (fire)", wait(lambda: st(l)["attributes"].get("effect") == "fire"))
    svc("light", "turn_off", {"entity_id": l})
    check("turn_off", wait(lambda: st(l)["state"] == "off"))
print(f"type {T} {NAME}: {ok} ok, {bad} bad")
