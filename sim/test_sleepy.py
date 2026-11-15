"""Battery (deep-sleep) devices: real ETCore.cpp + real hub.py on loopback.

A simulated deep sleep is the device process ending after it printed
"SLEEP <hub_confirmed> <awake_ms> <seconds>"; waking is starting it again with the
same RTC file (what survives deep sleep on a real chip). Deleting that file is a
power loss.

Run: sh sim/run-tests.sh
"""
from __future__ import annotations

import asyncio
import os
import tempfile
import time
import unittest

from harness import Device, MemoryCounterStore, make_hub, sender_from, wait_for
from test_etcore import PSK, packets

SLEEP_S = 1                      # the device says it sleeps this long between reports
TTL = 3 * SLEEP_S + 10           # what the library must ask the hub for


class Sleepy(unittest.IsolatedAsyncioTestCase):
    _next_ip = 150

    async def asyncSetUp(self):
        Sleepy._next_ip += 1
        self.ip = f"127.0.0.{Sleepy._next_ip}"
        self.devices: list[Device] = []
        self.hubs = []
        self.store = MemoryCounterStore()
        self.tmp = tempfile.TemporaryDirectory()
        self.rtc = os.path.join(self.tmp.name, "rtc.bin")

    async def asyncTearDown(self):
        for d in self.devices:
            d.stop()
        for h in self.hubs:
            await h.async_stop()
        self.tmp.cleanup()

    async def hub(self, psk=PSK):
        h = make_hub(asyncio.get_running_loop(), psk, self.store)
        await h.async_start()
        self.hubs.append(h)
        await asyncio.sleep(0.2)
        return h

    async def wake(self, dev_id="soil1", psk=PSK, value=None, timeout_ms=None, nosync=False, limit=6.0):
        """One wake: start the device, wait until it has gone back to sleep.
        Returns (hub_confirmed, awake_ms)."""
        env = {"ETCORE_SIM_SLEEP": str(SLEEP_S), "ETCORE_SIM_RTC": self.rtc, "ETCORE_SIM_RICH": "1"}
        if value is not None:
            env["ETCORE_SIM_VALUE"] = str(value)
        if timeout_ms is not None:
            env["ETCORE_SIM_SLEEP_TIMEOUT_MS"] = str(timeout_ms)
        if nosync:
            env["ETCORE_SIM_NOSYNC"] = "1"
        d = Device(dev_id, "sensor.moisture", self.ip, psk, env=env)
        self.devices.append(d)
        self.assertTrue(await wait_for(lambda: d.proc.poll() is not None, limit), "device never went to sleep")
        line = next((l for l in d.poll_lines() if l.startswith("SLEEP ")), None)
        self.assertIsNotNone(line, f"no SLEEP line: {d.lines}")
        _, confirmed, awake_ms, seconds = line.split()
        self.assertEqual(int(seconds), SLEEP_S)
        return confirmed == "1", int(awake_ms)

    async def test_wake_resumes_the_session_without_a_new_handshake(self):
        hub = await self.hub()
        confirmed, _ = await self.wake(value=40)
        self.assertTrue(confirmed, "cold boot was not confirmed by the hub")
        dev = hub.devices["soil1"]
        self.assertEqual(dev["state"].get("value"), 40.0)
        session, unproven = dev["rx_session"], hub._unproven_count
        hellos, boot = len(packets(hub.rec.rx, self.ip, "hello")), dev["boot"]

        await asyncio.sleep(SLEEP_S + 0.1)
        confirmed, awake_ms = await self.wake(value=41)
        self.assertTrue(confirmed, "wake was not confirmed by the hub")
        self.assertEqual(dev["state"].get("value"), 41.0)
        self.assertEqual(dev["rx_session"], session, "a wake must carry on the same session")
        self.assertEqual(dev["boot"], boot, "a wake is not a reboot")
        self.assertEqual(hub._unproven_count, unproven, "the hub challenged a wake")
        self.assertEqual(len(packets(hub.rec.rx, self.ip, "hello")), hellos, "a wake sent a hello nobody asked for")
        print(f"\n    wake: awake {awake_ms} ms, no handshake")
        self.assertLess(awake_ms, 400)

    async def test_second_wake_within_a_second_still_delivers(self):
        # The hub answers one "are you there" per device per second, so a wake
        # straight after another (a door contact firing twice) is not confirmed.
        # Its reading must still arrive, and it must not stay awake past its limit.
        hub = await self.hub()
        await self.wake(value=20)
        confirmed, awake_ms = await self.wake(value=21, timeout_ms=400)
        self.assertEqual(hub.devices["soil1"]["state"].get("value"), 21.0)
        self.assertLess(awake_ms, 700)
        await asyncio.sleep(SLEEP_S + 0.1)
        confirmed, _ = await self.wake(value=22)
        self.assertTrue(confirmed, "the wake after an unconfirmed one was not confirmed")
        self.assertEqual(hub.devices["soil1"]["state"].get("value"), 22.0)

    async def test_stays_online_while_asleep_then_goes_offline(self):
        hub = await self.hub()
        await self.wake()
        dev = hub.devices["soil1"]
        self.assertEqual(dev["lease_ttl"], TTL)
        await asyncio.sleep(7.5)  # a powered device (ttl 6 s) would be offline by now
        self.assertTrue(dev["online"], "went offline while asleep")
        t0 = time.monotonic()
        self.assertTrue(await wait_for(lambda: not dev["online"], TTL + 3), "never went offline after missing its wakes")
        print(f"\n    offline {7.5 + time.monotonic() - t0:.1f}s after the last wake (ttl {TTL}s)")

    async def test_cold_boot_reading_arrives_without_a_sync_handler(self):
        # The hub drops everything until a new session answered its challenge, so the
        # reading sent right after begin() is lost; the library must send it again.
        hub = await self.hub()
        confirmed, _ = await self.wake(value=55, nosync=True)
        self.assertTrue(confirmed)
        self.assertEqual((hub.devices["soil1"].get("state") or {}).get("value"), 55.0)

    async def test_wake_after_home_assistant_restarted(self):
        hub = await self.hub()
        await self.wake(value=10)
        await hub.async_stop()
        self.hubs.remove(hub)
        hub = await self.hub()  # knows nothing about the device's session
        confirmed, awake_ms = await self.wake(value=11, nosync=True)
        self.assertTrue(confirmed, "did not get through to a restarted hub")
        dev = hub.devices["soil1"]
        self.assertEqual((dev.get("state") or {}).get("value"), 11.0)
        self.assertTrue(dev.get("name"), "the restarted hub never got a hello")
        print(f"\n    wake after hub restart: awake {awake_ms} ms")

    async def test_power_loss_starts_a_new_session(self):
        hub = await self.hub()
        await self.wake(value=1)
        first = hub.devices["soil1"]["rx_session"]
        os.remove(self.rtc)
        confirmed, _ = await self.wake(value=2)
        self.assertTrue(confirmed)
        self.assertNotEqual(hub.devices["soil1"]["rx_session"], first)
        self.assertEqual(hub.devices["soil1"]["state"].get("value"), 2.0)

    async def test_no_hub_sleeps_anyway_and_recovers(self):
        confirmed, awake_ms = await self.wake(value=5, timeout_ms=600)
        self.assertFalse(confirmed)
        self.assertLess(awake_ms, 900, "stayed awake past its limit")
        hub = await self.hub()
        confirmed, _ = await self.wake(value=6)
        self.assertTrue(confirmed, "did not find the hub on the next wake")
        self.assertEqual(hub.devices["soil1"]["state"].get("value"), 6.0)

    async def test_recorded_wake_cannot_keep_a_dead_device_online(self):
        hub = await self.hub()
        await self.wake(value=7)
        await self.wake(value=8)
        dev = hub.devices["soil1"]
        old = [m["_raw"] for m in packets(hub.rec.rx, self.ip, "lease")]
        expires, replays = dev["lease_expires"], hub._replay_count
        s = sender_from(self.ip)
        try:
            for raw in old:
                s.sendto(raw, ("127.0.0.1", hub.port))
            await asyncio.sleep(0.4)
        finally:
            s.close()
        self.assertGreaterEqual(hub._replay_count, replays + len(old))
        self.assertEqual(dev["lease_expires"], expires, "a replayed wake extended the lease")

    async def test_works_without_encryption(self):
        hub = await self.hub(psk=None)
        confirmed, _ = await self.wake(psk=None, value=3)
        self.assertTrue(confirmed)
        await asyncio.sleep(SLEEP_S + 0.1)
        confirmed, _ = await self.wake(psk=None, value=4)
        self.assertTrue(confirmed)
        self.assertEqual(hub.devices["soil1"]["state"].get("value"), 4.0)
        self.assertEqual(hub.devices["soil1"]["lease_ttl"], TTL)


if __name__ == "__main__":
    unittest.main()
