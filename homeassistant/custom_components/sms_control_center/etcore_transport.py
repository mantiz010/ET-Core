"""Encrypted SMS transport; durable event dedupe, no automatic SMS resubmission."""
from __future__ import annotations
import asyncio
import copy
from datetime import timedelta
import re
import uuid
import time
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.helpers.storage import Store

GATEWAY = 'sim7600-sms-gateway'
PROFILE = 'sms_gateway_v1'


def validate(message, number):
    if not isinstance(message, str) or not message or len(message.encode()) > 160:
        raise HomeAssistantError('SMS must contain 1–160 ASCII bytes; split longer messages first')
    if any(ord(c) < 32 or ord(c) > 126 or c == '`' for c in message):
        raise HomeAssistantError('SMS gateway requires plain printable GSM-compatible text')
    if sum(2 if c in '^{}[]~|\\' else 1 for c in message) > 160:
        raise HomeAssistantError('SMS exceeds 160 GSM units; split the message first')
    if number and (len(number) > 31 or not re.fullmatch(r'\+?[0-9]+', number)):
        raise HomeAssistantError('Invalid SMS destination')


class EtCoreSmsTransport:
    def __init__(self, owner):
        self.owner = owner
        self.hass = owner.hass
        self.store = Store(self.hass, 1, 'sms_control_center.etcore')
        self.data = {'seen': [], 'requests': {}}
        self.hubs = []
        self.pending = {}
        self.lock = asyncio.Lock()
        self.closed = False
        self.health_at = 0
        self.boot = None
        self.unsub = None

    async def start(self):
        self.data = await self.store.async_load() or self.data
        self.owner.in_count = self.data.get('in_count', self.owner.in_count)
        self.owner.out_count = self.data.get('out_count', self.owner.out_count)
        self.bind()
        self.unsub = async_track_time_interval(self.hass, self.bind, timedelta(seconds=5))

    def bind(self, _now=None):
        for hub in self.hass.data.get('etcore', {}).values():
            if hub not in self.hubs:
                self.hubs.append(hub)
                hub.register_listener(self.message)

    def message(self, msg):
        if self.closed or msg.get('id') != GATEWAY or not msg.get('_encrypted') or msg.get('_replay'):
            return
        if msg.get('boot') != self.boot:
            self.boot = msg.get('boot')
            self.owner.modem_ready = False
            self.health_at = 0
        if msg.get('type') != 'state':
            return
        health = (msg.get('payload') or {}).get('sms_health')
        if isinstance(health, dict):
            self.owner.modem_ready = health.get('modem_ready') is True and health.get('storage_ok') is True
            self.health_at = time.monotonic()
        event = (msg.get('payload') or {}).get('sms_event')
        if isinstance(event, dict):
            self.hass.async_create_task(self.event(event))

    def hub(self):
        for hub in self.hass.data.get('etcore', {}).values():
            dev = hub.devices.get(GATEWAY, {})
            if dev.get('online') and dev.get('class') == 'bridge.sms' and (dev.get('hello') or {}).get('profile') == PROFILE:
                return hub
        raise HomeAssistantError('Encrypted SMS gateway is offline or has not announced its profile')

    async def stop(self):
        self.closed = True
        if self.unsub:
            self.unsub()
        for hub in self.hubs:
            # ET-Core 0.4.3 listener registration has no removal function.
            if self.message in hub._message_listeners:
                hub._message_listeners.remove(self.message)
        for future in self.pending.values():
            if not future.done():
                future.cancel()
        self.pending.clear()

    async def event(self, e):
        async with self.lock:
            event_id = e.get('event_id')
            if not isinstance(event_id, str) or not re.fullmatch(r'[0-9a-f]{12}-[0-9]{1,10}', event_id):
                return
            kind = e.get('kind')
            if kind == 'incoming':
                if not isinstance(e.get('sender'), str) or not isinstance(e.get('message'), str) or len(e['sender']) > 31 or len(e['message']) > 640:
                    return
            elif kind == 'result':
                if not isinstance(e.get('request_id'), str) or not re.fullmatch(r'[0-9a-fA-F-]{36}', e['request_id']):
                    return
                if e.get('status') not in ('queued', 'modem_accepted', 'submission_unknown', 'not_submitted') or e.get('request_id') not in self.data['requests']:
                    # An old request from before this HA install must not block its outbox forever.
                    if e.get('status') not in ('queued', 'modem_accepted', 'submission_unknown', 'not_submitted'):
                        return
            else:
                return
            if event_id not in self.data['seen']:
                before = copy.deepcopy(self.data)
                self.data['seen'] = (self.data['seen'] + [event_id])[-2048:]
                if kind == 'incoming':
                    self.data['in_count'] = self.owner.in_count + 1
                else:
                    request = self.data['requests'].get(e.get('request_id'))
                    if request and not (request.get('status') in ('modem_accepted','submission_unknown','not_submitted') and e['status']=='queued'):
                        request['status'] = e['status']
                # Save before firing automations. A crash in the small save→fire window
                # can lose an action; replaying a door/garage command is worse.
                try:
                    await self.store.async_save(self.data)
                except Exception:
                    self.data = before
                    raise
                self.owner.etcore_event(e)
                future = self.pending.get(e.get('request_id'))
                if future and not future.done():
                    future.set_result(e.get('status'))
                self.hass.bus.async_fire('sms_control_center_incoming' if kind == 'incoming' else 'sms_control_center_result', dict(e))
            try:
                self.hub().send_command(GATEWAY, 'bridge.sms', {'command':'ack_event', 'event_id':event_id})
            except HomeAssistantError:
                pass  # Gateway keeps the persisted event and retries it.

    async def send(self, message, number=None, topic=None):
        validate(message, number)
        if topic not in (None, '', self.owner.outgoing_topic, 'home/sms/send', 'home/sms/send2'):
            raise HomeAssistantError('Unknown SMS recipient route')
        hub = self.hub()
        if not self.owner.modem_ready or time.monotonic()-self.health_at > 60:
            raise HomeAssistantError('SMS modem readiness is unavailable or stale')
        request_id = str(uuid.uuid4())
        payload = {'command':'send_sms','request_id':request_id,'message':message,
                   'recipient':2 if topic == 'home/sms/send2' else 1}
        if number:
            payload['number'] = number
        async with self.lock:
            self.data['requests'][request_id] = {'status':'submitted_to_gateway'}
            if len(self.data['requests']) > 2048:
                self.data['requests'].pop(next(iter(self.data['requests'])))
            self.data['out_count'] = self.owner.out_count + 1
            await self.store.async_save(self.data)
            future = self.hass.loop.create_future()
            self.pending[request_id] = future
            self.owner.record_outgoing(message, number or '', topic or self.owner.outgoing_topic, request_id)
            hub.send_command(GATEWAY, 'bridge.sms', payload)
        try:
            status = await asyncio.wait_for(future, 10)
            if status not in ('queued','modem_accepted'):
                raise HomeAssistantError('SMS submission result is uncertain; no automatic retry')
        except TimeoutError as err:
            raise HomeAssistantError('SMS acceptance unconfirmed; do not automatically retry') from err
        finally:
            self.pending.pop(request_id, None)
