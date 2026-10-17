"""Wire round trip: HA's command packet to the C3 -> the C3's next packet back.
    wire_rtt.py <entity> <udp_port>"""
import subprocess, sys, time, re, statistics
sys.path.insert(0, "/home/mantiz010/ha-test")
from ha import call, token
E, PORT = sys.argv[1], sys.argv[2]
C3 = "172.168.1.112"
tok = token()
p = subprocess.Popen(["sudo", "-n", "tcpdump", "-n", "-tt", "-l", "-i", "any", f"udp port {PORT} and host {C3}"],
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
time.sleep(2)
for i in range(20):
    call("POST", f"/api/services/light/turn_{'on' if i % 2 == 0 else 'off'}", {"entity_id": E}, tok=tok)
    time.sleep(0.6)
time.sleep(1)
subprocess.run(["sudo", "-n", "pkill", "-INT", "-x", "tcpdump"])
out = p.communicate(timeout=10)[0]
pk = []
for line in out.splitlines():
    m = re.match(r"(\d+\.\d+) \S+ (In|Out)\s+IP (\S+) > (\S+): UDP, length (\d+)", line)
    if m: pk.append((float(m.group(1)), m.group(2), m.group(4), int(m.group(5))))
rtt = []
for i, (t, d, dst, n) in enumerate(pk):
    if d == "Out" and dst.startswith(C3):
        nxt = next(((t2, n2) for t2, d2, _, n2 in pk[i + 1:] if d2 == "In"), None)
        if nxt and nxt[0] - t < 1: rtt.append((nxt[0] - t) * 1000)
print(f"{E}: {len(rtt)} command round trips on the wire, median {statistics.median(rtt):.1f} ms, max {max(rtt):.1f} ms")
