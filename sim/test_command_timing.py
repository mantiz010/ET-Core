"""Exercise the real hub retry state machine with a deterministic clock.

No LAN traffic, shortened production timers, or hardware is used.
"""
import heapq
import json
import unittest
from harness import FakeHass, load_hub_module


class Timer:
    def __init__(self, when, callback, args):
        self.when, self.callback, self.args = when, callback, args
        self.cancelled = False

    def cancel(self):
        self.cancelled = True


class Clock:
    def __init__(self):
        self.now = 0.0
        self.queue = []
        self.serial = 0

    def call_later(self, delay, callback, *args):
        timer = Timer(self.now + delay, callback, args)
        self.serial += 1
        heapq.heappush(self.queue, (timer.when, self.serial, timer))
        return timer

    def advance(self, seconds):
        end = self.now + seconds
        while self.queue and self.queue[0][0] <= end:
            when, _, timer = heapq.heappop(self.queue)
            self.now = when
            if not timer.cancelled:
                timer.callback(*timer.args)
        self.now = end


class CommandTiming(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.module = load_hub_module()
        self.hub = self.module.EtCoreHub(FakeHass(self.clock), {})
        self.hub._transport = object()
        self.sent = []
        self.hub._send_raw = lambda data, addr: self.sent.append(json.loads(data))
        for name, transport in [('thread', 'thread'), ('wifi', 'wifi')]:
            self.hub._devices[name] = {
                'id': name, 'last_addr': '::1',
                'hello': {'transport': transport},
            }

    def send(self, device='thread', cls='firmware.ota'):
        return self.hub.send_command(device, cls, {'op': 'chunk'})

    def ack(self, cmd, device='thread', **fields):
        return self.hub._handle_ack(device, dict(cmd_id=cmd, ok=True, **fields))

    def test_one_second_reply_no_premature_retry_or_failure(self):
        cmd = self.send()
        self.clock.advance(1.0)
        self.assertEqual(len(self.sent), 1)
        self.assertIn(cmd, self.hub._pending)
        self.ack(cmd)
        self.clock.advance(10)
        self.assertEqual(len(self.sent), 1)
        self.assertEqual(self.hub._cmd_fail_count, 0)

    def test_lost_first_reply_retries_same_id_with_fresh_sequence(self):
        cmd = self.send()
        self.clock.advance(1.6)
        self.assertEqual(len(self.sent), 2)
        self.assertNotEqual(self.sent[0]['seq'], self.sent[1]['seq'])
        self.assertEqual(self.sent[0]['payload']['cmd_id'], self.sent[1]['payload']['cmd_id'])
        self.ack(cmd)
        self.clock.advance(10)
        self.assertEqual(len(self.sent), 2)
        self.assertEqual(self.hub._cmd_fail_count, 0)

    def test_missing_replies_fail_once_before_coordinator_deadline(self):
        cmd = self.send()
        self.clock.advance(4.49)
        self.assertIn(cmd, self.hub._pending)
        self.clock.advance(.02)
        self.assertNotIn(cmd, self.hub._pending)
        self.assertEqual(len(self.sent), 3)
        self.assertEqual(self.hub._cmd_fail_count, 1)
        self.clock.advance(10)
        self.assertEqual(self.hub._cmd_fail_count, 1)

    def test_wifi_ota_and_normal_thread_commands_keep_existing_timing(self):
        self.send('wifi')
        self.send('thread', 'switch')
        self.clock.advance(.31)
        self.assertEqual(len(self.sent), 4)
        self.clock.advance(.60)
        self.assertEqual(self.hub._cmd_fail_count, 2)

    def test_wrong_device_ack_does_not_cancel_retry(self):
        cmd = self.send()
        self.ack(cmd, device='wifi')
        self.clock.advance(1.6)
        self.assertEqual(len(self.sent), 2)
        self.assertIn(cmd, self.hub._pending)

    def test_retry_setting_is_local_not_in_wire_payload(self):
        self.send()
        self.assertNotIn('ack_timeout', self.sent[0])
        self.assertNotIn('ack_timeout', self.sent[0]['payload'])


if __name__ == '__main__':
    unittest.main()
