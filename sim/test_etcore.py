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


def site_announce(psk_hex, key_label, seq, payload, hub_session=b"\x07" * 7):
    """An announce encrypted the way the hub does it, with a chosen key label."""
    import base64, hashlib
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
    key = hashlib.sha256(bytes.fromhex(psk_hex) + key_label).digest()
    nonce = bytes([3]) + hub_session + seq.to_bytes(4, "little")
    out = ChaCha20Poly1305(key).encrypt(nonce, json.dumps(payload).encode(), f"v1|announce|hub|{seq}".encode())
    return json.dumps({"v": 1, "type": "announce", "id": "hub", "class": "hub", "seq": seq, "payload": {
        "_enc": 1, "alg": "C20P1305", "n": base64.b64encode(nonce).decode(),
        "ct": base64.b64encode(out[:-16]).decode(), "tag": base64.b64encode(out[-16:]).decode()}}).encode()


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

    async def test_bye_marks_device_offline_at_once(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayAD")
        d.stop()  # a planned restart (OTA, reboot): the device says bye
        t0 = time.monotonic()
        self.assertTrue(await wait_for(lambda: not self.online(hub, "relayAD"), 6), "never went offline")
        took = time.monotonic() - t0
        print(f"\n    offline {took:.2f}s after bye")
        self.assertLess(took, 1.0, "bye ignored: went offline only when the lease ran out")
        status = [e for _, n, e in hub.hass.bus.events if n == "etcore_device_status" and e["id"] == "relayAD"]
        self.assertEqual(status[-1]["reason"], "bye")

    async def test_recorded_bye_cannot_knock_device_offline(self):
        hub = await self.hub()
        ip = self.ip()
        d1 = Device("relayAE", "switch.relay", ip, PSK)
        self.devices.append(d1)
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayAE"), 5))
        d1.stop()
        self.assertTrue(await wait_for(lambda: packets(hub.rec.rx, ip, "bye"), 2))
        bye = packets(hub.rec.rx, ip, "bye")[-1]["_raw"]
        d2 = Device("relayAE", "switch.relay", ip, PSK)
        self.devices.append(d2)
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayAE"), 5))
        attacker = sender_from(ip)
        try:
            for _ in range(3):
                attacker.sendto(bye, ("127.0.0.1", PORT))
                await asyncio.sleep(0.2)
        finally:
            attacker.close()
        self.assertTrue(self.online(hub, "relayAE"), "a recorded bye knocked the device offline")

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
        await asyncio.sleep(1.5)  # all retries (300 ms x 2) done and saves written
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

    async def _online_device(self, hub, dev_id, **kw):
        d = self.device(dev_id, **kw)
        self.assertTrue(await wait_for(lambda: self.online(hub, dev_id), 5))
        return d

    async def test_ack_sent_by_library_when_sketch_forgets(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayL", env={"ETCORE_SIM_NOACK": "1"})
        hub.send_command("relayL", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: packets(hub.rec.rx, d.ip, "ack"), 2), "no ack")
        await asyncio.sleep(1.2)
        self.assertEqual(len(d.commands()), 1)
        self.assertEqual(hub._pending, {}, "hub still waiting for an ack")

    async def test_lost_command_is_resent(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayM")
        hub.drop_tx["cmd"] = 1  # first transmission lost
        hub.send_command("relayM", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 2), "command never arrived")
        await asyncio.sleep(1.2)
        self.assertEqual(len(d.commands()), 1, "executed more than once")
        self.assertEqual(hub._pending, {})

    async def test_lost_ack_resend_is_not_executed_twice(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayN")
        hub.drop_rx["ack"] = 1  # device's first ack lost
        hub.send_command("relayN", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: hub._pending == {}, 2), "never acked")
        await asyncio.sleep(0.5)
        self.assertEqual(len(d.commands()), 1, f"resend executed again: {d.commands()}")
        self.assertGreaterEqual(len(packets(hub.rec.tx, d.ip, "cmd")), 2, "hub never resent")

    async def test_dead_device_command_fails_visibly(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayO")
        d.kill()
        hub.send_command("relayO", "switch.relay", {"on": True})
        failed = lambda: [e for _, n, e in hub.hass.bus.events if n == "etcore_command_failed"]
        self.assertTrue(await wait_for(failed, 3), "no etcore_command_failed event")
        self.assertEqual(failed()[0]["id"], "relayO")
        self.assertEqual(len(packets(hub.rec.tx, d.ip, "cmd")), 3, "expected 1 send + 2 retries")

    async def test_each_command_gets_its_own_cmd_id(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayP")
        for on in (True, False, True):
            hub.send_command("relayP", "switch.relay", {"cmd_id": "same-from-entity", "on": on})
            await asyncio.sleep(0.2)
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 3, 2))
        ids = [json.loads(c)["cmd_id"] for c in d.commands()]
        self.assertEqual(len(set(ids)), 3, f"cmd_ids not unique: {ids}")

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

    async def test_leases_from_an_earlier_boot_cannot_fake_online(self):
        hub = await self.hub()
        ip = self.ip()
        d1 = Device("relayAB", "switch.relay", ip, PSK, lease_ms=500)
        self.devices.append(d1)
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayAB"), 5))
        await asyncio.sleep(4)
        old = [m["_raw"] for m in packets(hub.rec.rx, ip, "lease")]  # boot 1, recorded
        d1.kill()
        d2 = Device("relayAB", "switch.relay", ip, PSK, lease_ms=500)  # same device rebooted
        self.devices.append(d2)
        n = len(packets(hub.rec.rx, ip, "lease"))
        self.assertTrue(await wait_for(lambda: len(packets(hub.rec.rx, ip, "lease")) > n + 3, 5))
        d2.kill()  # unplugged for good
        attacker = sender_from(ip)
        try:
            for raw in old[-12:] * 2:  # boot-1 leases, in order (their seq rises)
                attacker.sendto(raw, ("127.0.0.1", PORT))
                await asyncio.sleep(0.5)
        finally:
            attacker.close()
        self.assertFalse(self.online(hub, "relayAB"), "leases from an earlier boot kept a dead device online")
        diag = hub.get_diag_snapshot()
        self.assertGreater(diag["unproven_dropped"], 0)
        self.assertEqual(diag["decrypt_failures"], 0, "replays/unproven counted as decrypt failures")

    async def test_flood_of_old_boot_packets_cannot_block_a_reboot(self):
        hub = await self.hub()
        ip = self.ip()
        old = []
        for _ in range(5):  # recordings from five earlier boots
            d = Device("relayAF", "switch.relay", ip, PSK)
            self.devices.append(d)
            self.assertTrue(await wait_for(lambda: packets(hub.rec.rx, ip, "lease")
                                           and len({m["payload"]["n"][:12] for m in packets(hub.rec.rx, ip, "lease")}) > len(old), 5))
            await asyncio.sleep(2.2)
            old.append(packets(hub.rec.rx, ip, "lease")[-1]["_raw"])
            d.kill()
        attacker = sender_from(self.ip())
        attacker.setblocking(False)
        stop = False

        async def flood():
            while not stop:
                for raw in old:
                    attacker.sendto(raw, ("127.0.0.1", PORT))
                await asyncio.sleep(0.005)

        task = asyncio.create_task(flood())
        try:
            await asyncio.sleep(1)
            d2 = Device("relayAF", "switch.relay", ip, PSK)
            self.devices.append(d2)
            ok = await wait_for(lambda: hub.devices.get("relayAF", {}).get("boot") == self._boot_of(d2, hub, ip), 8)
        finally:
            stop = True
            await task
            attacker.close()
        print(f"\n    flood of {len(old)} old boots: {hub._unproven_count} packets dropped, rebooted device online {ok}")
        self.assertTrue(ok, "a flood of old-boot packets kept the rebooted device from coming online")

    def _boot_of(self, d, hub, ip):
        for line in d.poll_lines():
            pass
        boots = [m["boot"] for m in packets(hub.rec.rx, ip, "hello")]
        return boots[-1] if boots else None

    async def test_without_a_key_everything_still_works(self):
        hub = await self.hub(psk=None)
        d = self.device("plainA", psk=None)
        self.assertTrue(await wait_for(lambda: self.online(hub, "plainA"), 5), "no-key device never online")
        self.assertEqual(hub.devices["plainA"].get("name"), "plainA")
        hub.send_command("plainA", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 2), "no-key command lost")
        self.assertTrue(await wait_for(lambda: hub._pending == {}, 2), "no-key ack lost")
        await hub.async_stop()
        self.hubs.remove(hub)
        hub = await self.hub(psk=None)
        self.assertTrue(await wait_for(lambda: self.online(hub, "plainA") and hub.devices["plainA"].get("name"), 5),
                        "no-key device not back after hub restart")
        d.stop()
        self.assertTrue(await wait_for(lambda: not self.online(hub, "plainA"), 1.5), "no-key bye ignored")

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

    async def test_forged_hello_cannot_redirect_commands(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayQ")
        attacker_ip = self.ip()
        attacker = sender_from(attacker_ip)
        forged = json.dumps({"v": 1, "type": "hello", "id": "relayQ", "class": "light.rgb", "boot": "x",
                             "seq": 1, "payload": {"name": "relayQ"}}).encode()
        try:
            attacker.sendto(forged, ("127.0.0.1", PORT))
            await asyncio.sleep(0.3)
        finally:
            attacker.close()
        self.assertEqual(hub.devices["relayQ"]["last_addr"], d.ip, "forged hello moved the device address")
        self.assertEqual(hub.devices["relayQ"]["class"], "switch.relay", "forged hello changed the class")
        hub.send_command("relayQ", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 2), "command went elsewhere")

    async def test_forged_hello_cannot_rename_device(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayW")
        attacker = sender_from(self.ip())
        forged = json.dumps({"v": 1, "type": "hello", "id": "relayW", "class": "switch.relay", "boot": "x",
                             "seq": 1, "payload": {"name": "Hacked"}}).encode()
        try:
            attacker.sendto(forged, ("127.0.0.1", PORT))
            await asyncio.sleep(0.3)
        finally:
            attacker.close()
        self.assertEqual(hub.devices["relayW"].get("name"), "relayW", "forged hello renamed the device")
        hello = packets(hub.rec.rx, d.ip, "hello")[0]
        self.assertEqual(hello["payload"].get("_enc"), 1, "hello sent in plaintext with a key set")

    async def _announce_attack(self, hub, d, packet_for):
        """Send announces from another address; the device must keep talking to the hub."""
        attacker_ip = self.ip()
        attacker = sender_from(attacker_ip)
        attacker.setblocking(False)
        stolen = 0
        try:
            end = time.monotonic() + 8
            while time.monotonic() < end:
                attacker.sendto(packet_for(attacker_ip), (d.ip, PORT))
                await asyncio.sleep(0.5)
                while True:
                    try:
                        data, a = attacker.recvfrom(4096)
                        stolen += a[0] == d.ip
                    except BlockingIOError:
                        break
        finally:
            attacker.close()
        self.assertEqual(stolen, 0, f"device sent {stolen} packets to the forged hub address")
        self.assertTrue(self.online(hub, d.id), "device went offline: its leases were redirected")

    async def test_forged_plaintext_announce_cannot_redirect_device(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayX")
        await self._announce_attack(hub, d, lambda ip: json.dumps(
            {"v": 1, "type": "announce", "id": "hub", "class": "hub", "seq": 99999,
             "payload": {"hello": True, "hub": ip}}).encode())

    async def test_replayed_genuine_announce_cannot_redirect_device(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayY")
        await hub.async_stop()
        self.hubs.remove(hub)
        hub = await self.hub()  # new hub: it announces to the device again
        self.assertTrue(await wait_for(lambda: packets(hub.rec.tx, d.ip, "announce"), 5))
        recorded = packets(hub.rec.tx, d.ip, "announce")[-1]["_raw"]
        self.assertTrue(await wait_for(lambda: self.online(hub, "relayY"), 5))
        await self._announce_attack(hub, d, lambda ip: recorded)

    async def test_device_named_hub_cannot_forge_site_announce(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayZ")
        # "hub" as a device id: its device key is sha256(psk || "hub").
        await self._announce_attack(hub, d, lambda ip: site_announce(PSK, b"hub", 4000000, {"hello": True, "hub": ip}))

    async def test_genuine_multicast_announce_alone_is_followed(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayAA")
        await asyncio.sleep(10.5)  # past the device's once-per-10-s hello limit
        await hub.async_stop()
        self.hubs.remove(hub)
        h2 = make_hub(asyncio.get_running_loop(), PSK, self.store)
        orig = h2._send_announce
        h2._send_announce = lambda target, **kw: None if kw.get("dev_id") else orig(target, **kw)
        await h2.async_start()  # only the encrypted multicast announce goes out
        self.hubs.append(h2)
        self.assertTrue(await wait_for(lambda: packets(h2.rec.rx, d.ip, "hello"), 3),
                        "device did not answer the hub's genuine multicast announce")

    async def test_commands_recover_after_ha_restored_from_backup(self):
        hub = await self.hub()
        backup = self.store.value  # "last night's backup" of HA storage
        d = await self._online_device(hub, "relayR")
        hub._seq = backup + 600  # commands sent since the backup: past the saved block
        for on in (True, False, True):
            hub.send_command("relayR", "switch.relay", {"on": on})
            await asyncio.sleep(0.2)
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 3, 2))
        await hub.async_stop()
        self.hubs.remove(hub)
        self.store.value = backup  # HA restored from the backup
        hub2 = await self.hub()
        self.assertTrue(await wait_for(lambda: self.online(hub2, "relayR"), 8))
        hub2.send_command("relayR", "switch.relay", {"on": False})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 4, 3),
                        "device ignored commands after HA was restored from a backup")
        self.assertEqual(json.loads(d.commands()[-1])["on"], False)

    async def test_names_learned_even_if_startup_multicast_is_lost(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayS")
        await hub.async_stop()
        self.hubs.remove(hub)
        h2 = make_hub(asyncio.get_running_loop(), PSK, self.store)
        h2.drop_tx["announce"] = 1  # the start-up multicast never arrives (WiFi)
        await h2.async_start()
        self.hubs.append(h2)
        self.assertTrue(await wait_for(lambda: h2.devices.get("relayS", {}).get("name") == "relayS", 6),
                        f"name not learned without the multicast: {h2.devices.get('relayS')}")

    async def test_new_hub_gets_current_state_without_waiting(self):
        hub = await self.hub()
        d = await self._online_device(hub, "relayT")
        hub.send_command("relayT", "switch.relay", {"on": True})
        self.assertTrue(await wait_for(lambda: len(d.commands()) == 1, 2))
        await hub.async_stop()
        self.hubs.remove(hub)
        hub2 = await self.hub()  # HA restarted: it must learn the device is ON
        got = lambda: [m for m in packets(hub2.rec.rx, d.ip, "state")]
        self.assertTrue(await wait_for(got, 4), "no state from the device after HA restart")

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

    async def test_session_uses_all_seven_spare_nonce_bytes(self):
        import base64
        hub = await self.hub()
        devs = [await self._online_device(hub, f"relayV{i}") for i in range(3)]
        hub.send_command("relayV0", "switch.relay", {"on": True})
        await asyncio.sleep(0.5)
        for d in devs:
            n = base64.b64decode(packets(hub.rec.rx, d.ip, "lease")[-1]["payload"]["n"])
            self.assertNotEqual(n[5:8], b"\0\0\0", f"device session only 4 bytes: {n.hex()}")
        n = base64.b64decode(packets(hub.rec.tx, devs[0].ip, "cmd")[-1]["payload"]["n"])
        self.assertNotEqual(n[5:8], b"\0\0\0", f"hub session only 4 bytes: {n.hex()}")

    async def test_device_finds_hub_again_when_it_goes_quiet(self):
        # The hub "moved" (e.g. HA got a new address): the device must stop
        # unicasting into the void and ask again by multicast; the hub answers.
        hub = await self.hub()
        d = await self._online_device(hub, "relayAC", env={"ETCORE_SIM_HUB_SILENT_MS": "3000"})
        before = len(packets(hub.rec.tx, d.ip, "announce"))
        g = group_listener()
        flagged = []
        try:
            end = time.monotonic() + 6
            while time.monotonic() < end:
                try:
                    data, a = g.recvfrom(4096)
                    m = json.loads(data)
                    if a[0] == d.ip and m.get("type") == "lease":
                        flagged.append(m)
                except BlockingIOError:
                    await asyncio.sleep(0.05)
        finally:
            g.close()
        self.assertTrue(flagged, "device never fell back to multicast after the hub went quiet")
        self.assertGreater(len(packets(hub.rec.tx, d.ip, "announce")), before,
                           "hub did not answer the device's search")
        self.assertTrue(self.online(hub, "relayAC"))

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
