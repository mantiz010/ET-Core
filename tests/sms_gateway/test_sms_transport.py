import asyncio,importlib.util,json,time,unittest
from unittest.mock import MagicMock,AsyncMock,patch
from pathlib import Path
from types import SimpleNamespace
spec=importlib.util.spec_from_file_location('sms_etcore_transport',Path(__file__).parents[2]/'homeassistant/custom_components/sms_control_center/etcore_transport.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
EID='aabbccddeeff-1';RID='01234567-89ab-cdef-0123-456789abcdef'
class TransportTests(unittest.IsolatedAsyncioTestCase):
 async def asyncSetUp(self):
  self.hass=SimpleNamespace(loop=asyncio.get_running_loop(),data={},bus=SimpleNamespace(async_fire=MagicMock()),async_create_task=asyncio.create_task)
  self.owner=SimpleNamespace(hass=self.hass,modem_ready=True,in_count=0,out_count=0,outgoing_topic='home/sms/send',etcore_event=MagicMock(),record_outgoing=MagicMock())
  with patch.object(m,'Store',return_value=SimpleNamespace(async_save=AsyncMock(),async_load=AsyncMock(return_value=None))):self.t=m.EtCoreSmsTransport(self.owner)
  self.hub=SimpleNamespace(devices={m.GATEWAY:{'online':True,'class':'bridge.sms','hello':{'profile':m.PROFILE}}},send_command=MagicMock())
  self.hass.data={'etcore':{'test':self.hub}};self.t.health_at=time.monotonic()
 async def test_dedup_incoming_after_persist(self):
  e={'event_id':EID,'kind':'incoming','sender':'+640000000','message':'status'}
  await self.t.event(e);await self.t.event(e)
  self.assertEqual(self.hass.bus.async_fire.call_count,1);self.assertEqual(self.hub.send_command.call_count,2)
  self.t.store.async_save.assert_awaited_once()
 async def test_restart_dedup(self):
  self.t.data['seen']=[EID];await self.t.event({'event_id':EID,'kind':'incoming','sender':'1','message':'status'})
  self.hass.bus.async_fire.assert_not_called();self.hub.send_command.assert_called_once()
 async def test_storage_failure_does_not_execute(self):
  self.t.store.async_save.side_effect=OSError('disk')
  with self.assertRaises(OSError):await self.t.event({'event_id':EID,'kind':'incoming','sender':'1','message':'status'})
  self.hass.bus.async_fire.assert_not_called();self.hub.send_command.assert_not_called()
 async def test_invalid_event(self):
  for e in [{'event_id':'bad','kind':'incoming'}, {'event_id':EID,'kind':'incoming','sender':3,'message':'status'}, {'event_id':EID,'kind':'result','status':'delivered'}]:await self.t.event(e)
  self.hass.bus.async_fire.assert_not_called();self.hub.send_command.assert_not_called()
 async def test_only_encrypted_live_messages(self):
  for extras in [{},{'_encrypted':False},{'_encrypted':True,'_replay':True}]:
   self.t.message({'id':m.GATEWAY,'type':'state','payload':{'sms_event':{}},**extras})
  self.hass.bus.async_fire.assert_not_called();self.assertFalse(self.t.data['seen'])
 async def test_send_queue_ack(self):
  def command(dev,cls,payload):
   if payload['command']=='send_sms':asyncio.create_task(self.t.event({'event_id':EID,'kind':'result','request_id':payload['request_id'],'status':'queued'}))
  self.hub.send_command.side_effect=command
  await self.t.send('Test text',topic='home/sms/send2')
  payload=self.hub.send_command.call_args_list[0].args[2]
  self.assertEqual(payload['recipient'],2);self.assertEqual(payload['message'],'Test text');self.assertNotIn('mqtt',payload)
  self.assertEqual(len(self.t.data['requests']),1)
 async def test_stale_readiness_blocks(self):
  self.t.health_at=time.monotonic()-61
  with self.assertRaises(m.HomeAssistantError):await self.t.send('test')
  self.hub.send_command.assert_not_called()
 async def test_offline_blocks(self):
  self.hub.devices[m.GATEWAY]['online']=False
  with self.assertRaises(m.HomeAssistantError):await self.t.send('test')
 async def test_result_order(self):
  self.t.data['requests'][RID]={'status':'modem_accepted'}
  await self.t.event({'event_id':EID,'kind':'result','request_id':RID,'status':'queued'})
  self.assertEqual(self.t.data['requests'][RID]['status'],'modem_accepted')
 async def test_validation(self):
  for body,num in [('',None),('a'*161,None),('^'*81,None),('hello\x1a',None),('é',None),('test','+123\"'),('`',None)]:
   with self.assertRaises(m.HomeAssistantError):m.validate(body,num)
  m.validate('a'*160,'+6400000');m.validate('^'*80,None)
 async def test_unknown_route_blocks(self):
  with self.assertRaises(m.HomeAssistantError):await self.t.send('test',topic='unrelated')
  self.hub.send_command.assert_not_called()
 async def test_old_result_ack_without_submission(self):
  await self.t.event({'event_id':EID,'kind':'result','request_id':RID,'status':'modem_accepted'})
  self.assertEqual(self.hub.send_command.call_args.args[2]['command'],'ack_event')
if __name__=='__main__':unittest.main()
