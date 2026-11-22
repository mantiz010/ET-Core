"""Run with the real HA Python environment; only the device transport is fake."""
import asyncio
import importlib
import math
import os
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import AsyncMock, patch

component = Path(os.environ.get("ETCORE_COMPONENT", Path(__file__).parents[1] / "homeassistant/custom_components/etcore"))
pkg = types.ModuleType("etcore_media_candidate")
pkg.__path__ = [str(component)]
sys.modules[pkg.__name__] = pkg
m = importlib.import_module(pkg.__name__ + ".media_player")
from homeassistant.exceptions import HomeAssistantError
from homeassistant.components.media_player import MediaPlayerEntityFeature as F, MediaPlayerState as S


class Hub:
    def __init__(self, caps=None):
        self.devices = {"audio1": {"name": "Audio Test", "online": True, "boot": "one",
                                  "fw": "test", "hello": {"media_capabilities": caps if caps is not None else list(m.FEATURES) + ["tts"]}}}
        self.sent, self.listeners, self.status_listeners, self.removed_listeners = [], [], [], []
    def send_command(self, *args): self.sent.append(args)
    def register_listener(self, cb): self.listeners.append(cb)
    def register_status_listener(self, cb): self.status_listeners.append(cb)
    def register_removed_listener(self, cb): self.removed_listeners.append(cb)


class MediaTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.h = Hub()
        self.e = m.EtCoreMediaPlayer(self.h, "audio1", self.h.devices["audio1"])

    def test_identity_and_all_existing_controls(self):
        self.assertEqual(self.e.unique_id, "etcore_audio1_media_player")
        for feature in [F.PLAY,F.PAUSE,F.STOP,F.PLAY_MEDIA,F.VOLUME_SET,F.VOLUME_STEP,F.TURN_ON,F.TURN_OFF]:
            self.assertTrue(self.e.supported_features & feature)
        self.assertEqual(self.e.supported_features,F(22405))
        self.assertFalse(self.e.supported_features & F.SEEK)
        self.assertTrue(self.e.available)
        self.assertIsNone(self.e.state)

    def test_reported_state_is_validated(self):
        self.e.apply_state({"state":"paused","volume":0.3,"position":12,"duration":60,"title":"Track"})
        self.assertEqual((self.e.state,self.e.volume_level,self.e.media_title),(S.PAUSED,0.3,"Track"))
        self.assertIsNotNone(self.e.media_position_updated_at)
        for bad in [True,False,-1,2,float('nan'),float('inf'),'0.4',10**1000]:
            self.e.apply_state({"volume":bad,"state":"invented"})
            self.assertEqual(self.e.volume_level,0.3)
            self.assertEqual(self.e.state,S.PAUSED)

    def test_explicit_unknown_values_clear_stale_metadata(self):
        self.e.apply_state({'position':12,'duration':60,'volume':0.3,'error':'stream_connect_failed'})
        self.assertEqual(self.e.extra_state_attributes['playback_error'],'stream_connect_failed')
        self.e.apply_state({'position':None,'duration':None,'error':None})
        self.assertIsNone(self.e.media_position);self.assertIsNone(self.e.media_position_updated_at)
        self.assertIsNone(self.e.media_duration);self.assertEqual(self.e.extra_state_attributes,{})

    async def test_controls_do_not_optimistically_change_reported_state(self):
        self.e.apply_state({"state":"paused","volume":0.3})
        for fn in [self.e.async_media_play,self.e.async_media_pause,self.e.async_media_stop,self.e.async_turn_off,self.e.async_turn_on]:await fn()
        await self.e.async_set_volume_level(0.5)
        self.assertEqual([x[2]["op"] for x in self.h.sent],["play","pause","stop","power","power","volume"])
        self.assertEqual((self.e.state,self.e.volume_level),(S.PAUSED,0.3))

    async def test_volume_steps_retain_five_percent_behavior(self):
        self.e.apply_state({"volume":0.3})
        await self.e.async_volume_up()
        self.assertAlmostEqual(self.h.sent[-1][2]['volume'],0.35)
        await self.e.async_volume_down()
        self.assertAlmostEqual(self.h.sent[-1][2]['volume'],0.25)

    async def test_bad_volume_never_sends(self):
        for bad in [True,-0.1,1.1,float('nan'),float('inf'),'0.2',10**1000]:
            with self.assertRaises(HomeAssistantError):await self.e.async_set_volume_level(bad)
        self.assertEqual(self.h.sent,[])

    async def test_offline_and_unadvertised_controls_are_rejected(self):
        self.h.devices['audio1']['online']=False
        with self.assertRaises(HomeAssistantError):await self.e.async_media_play()
        self.h.devices['audio1']['online']=True
        self.h.devices['audio1']['hello']['media_capabilities']=['stop']
        self.e.update_info(self.h.devices['audio1'])
        with self.assertRaises(HomeAssistantError):await self.e.async_media_pause()
        self.assertEqual(self.h.sent,[])

    async def test_http_url_playback(self):
        with patch.object(m,'async_process_play_media_url',side_effect=lambda h,url:url):
            await self.e.async_play_media('music','https://audio.example/test.mp3')
        self.assertEqual(self.h.sent[-1][2],{'op':'play_media','url':'https://audio.example/test.mp3'})

    async def test_ha_tts_media_source_is_resolved_for_target(self):
        self.e.entity_id='media_player.audio_test'
        resolver=AsyncMock(return_value=types.SimpleNamespace(url='/api/tts/test.mp3',mime_type='audio/mpeg'))
        with patch.object(m.media_source,'async_resolve_media',resolver),patch.object(m,'async_process_play_media_url',return_value='http://ha.example/api/tts/test.mp3'):
            await self.e.async_play_media('music','media-source://tts/test')
        self.assertEqual(resolver.call_args.kwargs['target_media_player'],'media_player.audio_test')
        self.assertEqual(self.h.sent[-1][2]['url'],'http://ha.example/api/tts/test.mp3')

    async def test_direct_tts_is_bounded_and_language_checked(self):
        await self.e.async_play_media('tts','Hello',extra={'language':'en-NZ'})
        self.assertEqual(self.h.sent[-1][2],{'op':'tts','text':'Hello','language':'en-NZ'})
        before=len(self.h.sent)
        for text,extra in [('x'*1000,{}),('x'*700,{}),('bad\x00text',{}),('雷'*200,{}),('hello',{'language':'../../x'})]:
            with self.assertRaises(HomeAssistantError):await self.e.async_play_media('tts',text,extra=extra)
        self.assertEqual(len(self.h.sent),before)

    async def test_unsafe_and_oversized_urls_do_not_send(self):
        with patch.object(m,'async_process_play_media_url',side_effect=lambda h,url:url):
            for url in ['file:///etc/passwd','ftp://a/test','http://user:pass@example/a','http://a/\nx','https://a/'+'x'*1000]:
                with self.assertRaises(HomeAssistantError):await self.e.async_play_media('music',url)
        self.assertEqual(self.h.sent,[])

    def test_reboot_clears_prior_playback_and_offline_keeps_lease_truth(self):
        self.e.apply_state({'state':'playing','volume':0.8,'position':8})
        self.h.devices['audio1']['boot']='two';self.e.update_info(self.h.devices['audio1'])
        self.assertIsNone(self.e.state);self.assertIsNone(self.e.volume_level)
        self.h.devices['audio1']['online']=False;self.e.refresh_availability();self.assertFalse(self.e.available)

    async def test_lifecycle_discovery_and_removal(self):
        hass=types.SimpleNamespace(data={m.DOMAIN:{'entry':self.h}});entry=types.SimpleNamespace(entry_id='entry');added=[]
        await m.async_setup_entry(hass,entry,added.extend)
        receive=self.h.listeners[0]
        receive({'type':'state','class':'switch.relay','id':'audio1','payload':{}});self.assertEqual(added,[])
        msg={'type':'hello','class':m.DEVICE_CLASS,'id':'audio1','payload':{}}
        receive(msg);receive(msg);self.assertEqual(len(added),1)
        receive({'type':'state','class':m.DEVICE_CLASS,'id':'audio1','payload':{'state':'playing','volume':0.2}})
        self.assertEqual(added[0].state,S.PLAYING)
        self.h.removed_listeners[0]('audio1');receive(msg);self.assertEqual(len(added),2)

    def test_media_logs_redact_spoken_text_and_signed_urls(self):
        hub_module=importlib.import_module(pkg.__name__+'.hub')
        payload={'op':'play_media','url':'http://example/audio?token=private','text':'private words','volume':0.5}
        safe=hub_module._event_payload(m.DEVICE_CLASS,payload)
        self.assertEqual(safe['url'],'<redacted>');self.assertEqual(safe['text'],'<redacted>')
        self.assertEqual(safe['volume'],0.5)
        self.assertIn('token=private',payload['url'])
        self.assertEqual(hub_module._event_payload('sensor.other',payload),payload)

    async def test_missing_or_malformed_capability_hello_creates_nothing(self):
        hass=types.SimpleNamespace(data={m.DOMAIN:{'entry':self.h}});added=[]
        await m.async_setup_entry(hass,types.SimpleNamespace(entry_id='entry'),added.extend)
        for caps in [None,'play',[42],[],['unsupported']]:
            self.h.devices['audio1']['hello']['media_capabilities']=caps
            self.h.listeners[0]({'type':'hello','class':m.DEVICE_CLASS,'id':'audio1','payload':{}})
        self.assertEqual(added,[])

if __name__=='__main__':unittest.main()
