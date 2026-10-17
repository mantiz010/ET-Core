"""ET-Core simulator tests: real ETCore.cpp devices + real hub.py on loopback.

Run: sim/build.sh && python3 -m unittest -v sim/test_etcore.py
"""
from __future__ import annotations

import asyncio
import json
import os
import socket
import time
import unittest

from harness import Device, MemoryCounterStore, PORT, group_listener, make_hub, sender_from, wait_for

PSK = os.urandom(32).hex()  # test key only


def packets(records, ip, mtype):
    """Parsed packets of one type from/to one address (raw bytes kept as m["_raw"])."""
    out = []
    for raw, addr in records:
        if addr[0] != ip:
            continue
        try:
            m = json.loads(raw)
        except ValueError:
            continue
        if m.get("type") == mtype:
            m["_raw"] = raw
            out.append(m)
    return out


class EtCoreSim(unittest.IsolatedAsyncioTestCase):
    _next_ip = 10

    def ip(self):
        EtCoreSim._next_ip += 1
        return f"127.0.0.{EtCoreSim._next_ip}"

    async def asyncSetUp(self):
        self.devices: list[Device] = []
        self.hubs = []
        self.store = MemoryCounterStore()  # Home Assistant's storage, kept across hub restarts

    async def asyncTearDown(self):
        for d in self.devices:
            d.stop()
        for h in self.hubs:
            await h.async_stop()

    async def hub(self, psk=PSK):
        h = make_hub(asyncio.get_running_loop(), psk, self.store)
        await h.async_start()
        self.hubs.append(h)
        await asyncio.sleep(0.2)
        return h

    def device(self, dev_id, cls="switch.relay", psk=PSK, **kw):
        d = Device(dev_id, cls, self.ip(), psk, **kw)
        self.devices.append(d)
        return d

    @staticmethod
    def online(hub, dev_id):
        return bool(hub.devices.get(dev_id, {}).get("online"))

    async def test_online_then_offline_after_unplug(self):
        hub = await self.hub()
        d = self.device("relayA")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayA"), 5), "never came online")
        d.kill()
        t0 = time.monotonic()
        self.assertTrue(await wait_for(lambda: not self.online(hub, "relayA"), 15), "never went offline")
        took = time.monotonic() - t0
        print(f"\n    offline detected {took:.1f}s after unplug (ttl 6s)")
        self.assertLess(took, 8.5)

    async def test_command_reaches_device_and_is_acked(self):
        hub = await self.hub()
        d = self.device("relayB")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayB"), 5))
        hub.send_command("relayB", "switch.relay", {"cmd_id": "c1", "on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 3), "device got no command")
        self.assertEqual(json.loads(d.commands()[0])["on"], True)
        self.assertTrue(await wait_for(lambda: packets(hub.rec.rx, d.ip, "ack"), 3), "no ack from device")

    async def test_cmd_seq_never_goes_back_across_restarts_and_blocks(self):
        hub = await self.hub()
        hub._devices["ghost"] = {"id": "ghost", "last_addr": "127.0.0.250"}  # nobody listens there
        for _ in range(2500):
            hub.send_command("ghost", "switch.relay", {"on": True})
        await asyncio.sleep(0.1)
        last = hub._seq
        self.assertGreater(self.store.value, last, "saved reservation fell behind the seq in use")
        await hub.async_stop()
        self.hubs.remove(hub)
        hub2 = await self.hub()
        hub2._devices["ghost"] = {"id": "ghost", "last_addr": "127.0.0.250"}
        hub2.send_command("ghost", "switch.relay", {"on": True})
        self.assertGreater(hub2._seq, last, "seq went backwards after restart")

    async def test_hub_stop_leaves_no_thread_behind(self):
        import threading
        before = threading.active_count()
        for _ in range(4):  # e.g. the integration being reloaded
            h = make_hub(asyncio.get_running_loop(), PSK, self.store)
            await h.async_start()
            await asyncio.sleep(0.1)
            await h.async_stop()
        await asyncio.sleep(0.2)
        self.assertLessEqual(threading.active_count(), before, "hub stop left receiver threads running")

    async def test_wrong_key_device_does_not_flood_the_log(self):
        await self.hub()
        with self.assertLogs("etcore.hub", level="WARNING") as logs:
            self.device("relayK", psk=os.urandom(32).hex(), lease_ms=500)  # wrong key, fast leases
            await asyncio.sleep(4)
        warnings = [r for r in logs.records if "relayK" in r.getMessage()]
        self.assertEqual(len(warnings), 1, f"{len(warnings)} warnings for one wrong-key device in 4 s")

    async def test_bug1_commands_still_work_after_ha_restart(self):
        hub = await self.hub()
        d = self.device("relayC")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayC"), 5))
        for i in range(3):
            hub.send_command("relayC", "switch.relay", {"cmd_id": f"a{i}", "on": i % 2 == 0})
            await asyncio.sleep(0.2)
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 3, 3))
        await hub.async_stop()
        self.hubs.remove(hub)
        hub2 = await self.hub()  # Home Assistant restarted
        self.assertTrue(await wait_for(lambda: self.online(hub2, "relayC"), 8), "device not seen by new hub")
        hub2.send_command("relayC", "switch.relay", {"cmd_id": "after-restart", "on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 4, 3),
                        "device ignored the command after HA restart")

    async def test_bug2_replayed_lease_cannot_keep_unplugged_device_online(self):
        hub = await self.hub()
        d = self.device("relayD")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayD"), 5))
        await asyncio.sleep(2.5)
        leases = [m["_raw"] for m in packets(hub.rec.rx, d.ip, "lease")]
        self.assertTrue(leases, "captured no lease")
        d.kill()
        attacker = sender_from(d.ip)
        try:
            end = time.monotonic() + 12
            while time.monotonic() < end:
                attacker.sendto(leases[-1], ("127.0.0.1", PORT))
                await asyncio.sleep(1)
        finally:
            attacker.close()
        self.assertFalse(self.online(hub, "relayD"), "replayed lease kept a dead device online")

    async def test_fix0_forged_plaintext_lease_ignored_when_encrypted(self):
        hub = await self.hub()
        d = self.device("relayH")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayH"), 5))
        d.kill()
        attacker = sender_from(d.ip)
        forged = json.dumps({"v": 1, "type": "lease", "id": "relayH", "class": "switch.relay",
                             "boot": "0", "seq": 999999, "payload": {"ttl": 6}}).encode()
        try:
            end = time.monotonic() + 12
            while time.monotonic() < end:
                attacker.sendto(forged, ("127.0.0.1", PORT))
                await asyncio.sleep(1)
        finally:
            attacker.close()
        self.assertFalse(self.online(hub, "relayH"), "forged plaintext lease kept a dead device online")

    async def test_fix_relabelled_packet_cannot_pass_as_lease(self):
        hub = await self.hub()
        d = self.device("relayI")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayI"), 5))
        hub.send_command("relayI", "switch.relay", {"cmd_id": "x", "on": True})
        self.assertTrue(await wait_for(lambda: packets(hub.rec.rx, d.ip, "ack"), 3))
        ack = packets(hub.rec.rx, d.ip, "ack")[-1]
        ack.pop("_raw")
        d.kill()
        await wait_for(lambda: not self.online(hub, "relayI"), 10)
        attacker = sender_from(d.ip)
        try:
            for i in range(4):  # an encrypted ack re-sent with "type": "lease"
                fake = dict(ack, type="lease", seq=ack["seq"] + 100 + i)
                attacker.sendto(json.dumps(fake).encode(), ("127.0.0.1", PORT))
                fake = dict(ack, type="lease")
                attacker.sendto(json.dumps(fake).encode(), ("127.0.0.1", PORT))
                await asyncio.sleep(0.3)
        finally:
            attacker.close()
        self.assertFalse(self.online(hub, "relayI"), "an ack relabelled as lease brought the device online")

    async def test_fix_recorded_command_is_dead_after_device_reboot(self):
        hub = await self.hub()
        ip = self.ip()
        d = Device("relayJ", "switch.relay", ip, PSK)
        self.devices.append(d)
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayJ"), 5))
        hub.send_command("relayJ", "switch.relay", {"cmd_id": "rec", "on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 3))
        recorded = packets(hub.rec.tx, ip, "cmd")[-1]["_raw"]
        d.kill()
        d2 = Device("relayJ", "switch.relay", ip, PSK)  # same device, rebooted
        self.devices.append(d2)
        self.assertTrue(await wait_for(lambda: any(l.startswith("READY") for l in d2.poll_lines()), 3))
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.sendto(recorded, (ip, PORT))
            await asyncio.sleep(0.8)
        finally:
            s.close()
        self.assertEqual(d2.commands(), [], "a command recorded before the reboot was executed")
        # A fresh command from the hub still works once it has seen the new boot.
        await asyncio.sleep(2.5)
        hub.send_command("relayJ", "switch.relay", {"cmd_id": "new", "on": False})
        self.assertTrue(await wait_for(lambda: len(d2.commands()) == 1, 3), "fresh command after reboot failed")

    async def test_bug3_replayed_command_is_rejected(self):
        hub = await self.hub()
        d = self.device("relayE")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayE"), 5))
        hub.send_command("relayE", "switch.relay", {"cmd_id": "real", "on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 3))
        msg = packets(hub.rec.tx, d.ip, "cmd")[-1]
        msg.pop("_raw")
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            for seq in (msg["seq"], msg["seq"] + 1000, 0):
                m = dict(msg, seq=seq)
                s.sendto(json.dumps(m).encode(), (d.ip, PORT))
                await asyncio.sleep(0.3)
        finally:
            s.close()
        await asyncio.sleep(0.5)
        self.assertEqual(len(d.commands()), 1, f"replayed command executed: {d.commands()}")

    async def test_bug4_leases_go_unicast_once_hub_is_known(self):
        hub = await self.hub()
        d = self.device("relayF")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayF"), 5))
        await asyncio.sleep(3)
        g = group_listener()
        try:
            end, n = time.monotonic() + 5, 0
            while time.monotonic() < end:
                try:
                    data, a = g.recvfrom(4096)
                    if a[0] == d.ip and json.loads(data).get("type") == "lease":
                        n += 1
                except BlockingIOError:
                    await asyncio.sleep(0.05)
        finally:
            g.close()
        self.assertEqual(n, 0, f"{n} leases multicast in 5s while the hub was known")

    async def test_bug5_new_hub_learns_device_name_without_device_reboot(self):
        hub = await self.hub()
        d = self.device("relayG")
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayG"), 5))
        self.assertEqual(hub.devices["relayG"].get("name"), "relayG")
        await hub.async_stop()
        self.hubs.remove(hub)
        hub2 = await self.hub()
        self.assertTrue(await wait_for(lambda: hub2.devices.get("relayG", {}).get("name") == "relayG", 5),
                        f"new hub never learned the name: {hub2.devices.get('relayG')}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
