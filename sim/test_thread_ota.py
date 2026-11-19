import asyncio,base64,copy,hashlib,importlib,json,struct,sys,types,unittest
from pathlib import Path
from unittest.mock import patch
R=Path(__file__).resolve().parents[1]/'homeassistant/custom_components/etcore'
pkg=types.ModuleType('thread_ota_tests');pkg.__path__=[str(R)];sys.modules[pkg.__name__]=pkg
m=importlib.import_module(pkg.__name__+'.thread_ota')
BOARD='esp32c6-dev-4m';VERSION='thread-preview-0.4'
def image(payload_bytes=256):
 header=bytearray(24);header[0]=0xE9;header[1]=1;header[3]=0x20;header[12]=13;header[23]=1
 data=bytearray(payload_bytes);struct.pack_into('<I',data,0,0xABCD5432)
 for pos,text in [(16,'3.3.11'),(48,'C6ThreadOTA'),(128,VERSION),(160,BOARD),(192,m.PROTOCOL)]:data[pos:pos+len(text)+1]=text.encode()+b'\0'
 out=header+struct.pack('<II',0x42000000,len(data))+data;checksum=0xEF
 for b in data:checksum^=b
 out+=b'\0'*(15-len(out)%16)+bytes([checksum]);out+=hashlib.sha256(out).digest();return bytes(out)
def device():return {'id':'bench','online':True,'rx_session':'oldboot','fw':'thread-preview-0.3','hello':{'transport':'thread','ota':{'ready':True,'protocol':m.PROTOCOL,'board':BOARD,'max_image_size':1310720,'max_chunk':192}}}
class Hub:
 def __init__(self):
  self.hass=types.SimpleNamespace(loop=asyncio.get_running_loop());self._psk=b'x'*32;self.devices={'bench':device()};self.sent=[];self.listener=None;self.reject=None;self.health=True;self.reboot_version=VERSION
 def register_listener(self,cb):self.listener=cb
 def send_command(self,dev,cls,payload):
  self.sent.append(copy.deepcopy(payload));cmd=str(len(self.sent));ok=payload['op']!=self.reject
  if payload['op']=='commit' and ok:self.devices['bench'].update(rx_session='newboot',fw=self.reboot_version)
  if payload['op']=='health':ok=self.health
  self.hass.loop.call_soon(self.listener,{'_encrypted':True,'type':'ack','id':dev,'payload':{'cmd_id':cmd,'ok':ok}});return cmd
class Images(unittest.TestCase):
 def test_valid_application(self):self.assertEqual(m.validate_image(image(),board=BOARD,version=VERSION,capability=device()['hello']['ota'],current='old')['sha256'],hashlib.sha256(image()).hexdigest())
 def test_bad_images(self):
  for data in [b'zip',b'\xff'*4096,image()[:-1],image()+b'junk',bytes([0])+image()[1:]]:
   with self.assertRaises(ValueError):m.validate_image(data,board=BOARD,version=VERSION,capability=device()['hello']['ota'],current='old')
 def test_wrong_board_chip_version_and_capacity(self):
  for overrides in [{'board':'esp32s3'},{'version':'missing-version'},{'version':'old'},{'capability':{}},{'capability':dict(device()['hello']['ota'],max_image_size=100)}]:
   args=dict(board=BOARD,version=VERSION,capability=device()['hello']['ota'],current='old');args.update(overrides)
   with self.assertRaises(ValueError):m.validate_image(image(),**args)
  data=bytearray(image());data[12]=9
  with self.assertRaises(ValueError):m.validate_image(bytes(data),board=BOARD,version=VERSION,capability=device()['hello']['ota'],current='old')
class Transfer(unittest.IsolatedAsyncioTestCase):
 async def asyncSetUp(self):self.hub=Hub();self.manager=m.ThreadOTA(self.hub);self.addAsyncCleanup(self.manager.close)
 def prepare(self):return self.manager.prepare('bench',image(),BOARD,VERSION,'admin')
 def install(self,p):return self.manager.install('bench',p['plan_id'],p['sha256'],'admin')
 async def test_review_does_not_send_commands(self):self.prepare();self.assertEqual(self.hub.sent,[])
 async def test_explicit_confirm_exact_owner_hash_and_boot(self):
  p=self.prepare()
  for owner,sha in [('other',p['sha256']),('admin','0'*64)]:
   with self.assertRaises(ValueError):self.manager.install('bench',p['plan_id'],sha,owner)
  self.hub.devices['bench']['rx_session']='changed'
  with self.assertRaises(ValueError):self.install(p)
  self.assertEqual(self.hub.sent,[])
 async def test_expired_review(self):
  p=self.prepare();self.manager.expire_plan()
  with self.assertRaises(ValueError):self.install(p)
 async def test_success_requires_matching_boot_and_health(self):
  job=self.install(self.prepare());await self.manager.tasks['bench'];self.assertEqual(self.manager.jobs['bench']['phase'],'succeeded')
  chunks=[x for x in self.hub.sent if x['op']=='chunk'];self.assertEqual(b''.join(base64.b64decode(x['data']) for x in chunks),image());self.assertTrue(all(len(base64.b64decode(x['data']))<=192 for x in chunks));self.assertEqual(self.hub.sent[-1]['op'],'health')
 async def test_failure_before_commit_aborts_without_activation(self):
  self.hub.reject='chunk';self.install(self.prepare());await self.manager.tasks['bench'];self.assertEqual(self.manager.jobs['bench']['phase'],'failed');self.assertNotIn('commit',[p['op'] for p in self.hub.sent]);self.assertEqual(self.hub.sent[-1]['op'],'abort')
 async def test_wrong_post_reboot_version_is_not_success(self):
  self.hub.reboot_version='unexpected';self.install(self.prepare());await self.manager.tasks['bench'];self.assertEqual(self.manager.jobs['bench']['phase'],'unconfirmed');self.assertNotIn('health',[p['op'] for p in self.hub.sent])
 async def test_negative_health_is_not_success(self):
  self.hub.health=False;self.install(self.prepare());await self.manager.tasks['bench'];self.assertEqual(self.manager.jobs['bench']['phase'],'unconfirmed')
 async def test_unencrypted_and_wrong_device_ack_ignored(self):
  f=self.hub.hass.loop.create_future();self.manager.waiters['pending']=('bench',f)
  for encrypted,dev in [(False,'bench'),(True,'other')]:self.manager.receive({'_encrypted':encrypted,'type':'ack','id':dev,'payload':{'cmd_id':'pending','ok':True}})
  self.assertFalse(f.done())
 async def test_offline_or_plaintext_refused(self):
  self.hub._psk=None
  with self.assertRaises(ValueError):self.prepare()
 async def test_close_cancels_transfer_without_success(self):
  self.hub.send_command=lambda *args:'never-acked';self.install(self.prepare());await asyncio.sleep(.01);await self.manager.close();self.assertEqual(self.manager.jobs['bench']['phase'],'interrupted')
 async def test_storage_failure_prevents_flash_begin(self):
  async def fail(_):raise OSError('disk failure')
  self.manager.persist=fail;self.install(self.prepare());await self.manager.tasks['bench']
  self.assertNotIn('begin',[p['op'] for p in self.hub.sent]);self.assertEqual(self.manager.jobs['bench']['phase'],'failed')
 async def test_timeout_has_visible_error_and_does_not_activate(self):
  async def timeout(dev,payload):
   if payload['op']!='abort':raise asyncio.TimeoutError()
  self.manager.command=timeout
  self.install(self.prepare());await self.manager.tasks['bench']
  job=self.manager.jobs['bench'];self.assertEqual(job['phase'],'failed');self.assertEqual(job['error'],'TimeoutError');self.assertFalse(job.get('commit_sent',False))
 async def test_restored_active_jobs_are_interrupted(self):
  self.manager.restore({'bench':{'phase':'transferring','progress':20}})
  self.assertEqual(self.manager.jobs['bench']['phase'],'interrupted');self.assertEqual(self.hub.devices['bench']['ota_job']['phase'],'interrupted');self.assertEqual(self.hub.sent,[])
 async def test_stale_counter_reply_waits_for_hub_resend(self):
  f=self.hub.hass.loop.create_future();self.manager.waiters['pending']=('bench',f)
  self.manager.receive({'_encrypted':True,'type':'ack','id':'bench','payload':{'cmd_id':'pending','ok':False,'err':'stale'}})
  self.assertFalse(f.done())
class LatencyHub(Hub):
 def __init__(self,fast):
  super().__init__();self.inflight=0;self.maximum=0
  if fast:self.devices['bench']['hello']['ota'].update(max_chunk=288,max_window=2)
 def send_command(self,dev,cls,payload):
  self.sent.append(copy.deepcopy(payload));cmd=str(len(self.sent));self.inflight+=1;self.maximum=max(self.maximum,self.inflight)
  def deliver():
   self.inflight-=1
   if payload['op']=='commit':self.devices['bench'].update(rx_session='newboot',fw=VERSION)
   self.listener({'_encrypted':True,'type':'ack','id':dev,'payload':{'cmd_id':cmd,'ok':payload['op']!=self.reject}})
  # Vary ACK order as well as introducing a measurable round-trip cost.
  delay=.012 if payload['op']!='chunk' or payload.get('offset',0)%576 else .016
  self.hass.loop.call_later(delay,deliver);return cmd
class WindowTests(unittest.IsolatedAsyncioTestCase):
 async def run_transfer(self,fast,reject=None):
  hub=LatencyHub(fast);hub.reject=reject;manager=m.ThreadOTA(hub)
  try:
   with patch.object(m,'ENABLE_WINDOWED_PREVIEW',True):
    plan=manager.prepare('bench',image(16384),BOARD,VERSION,'admin')
    start=asyncio.get_running_loop().time();manager.install('bench',plan['plan_id'],plan['sha256'],'admin');await manager.tasks['bench'];elapsed=asyncio.get_running_loop().time()-start
   return hub,manager.jobs['bench'],elapsed
  finally:await manager.close()
 async def test_legacy_stays_serial_and_fast_window_is_bounded(self):
  old,oldjob,slow=await self.run_transfer(False);new,newjob,fast=await self.run_transfer(True)
  self.assertEqual(old.maximum,1);self.assertEqual(oldjob['chunk_size'],192);self.assertEqual(oldjob['window'],1)
  self.assertEqual(new.maximum,2);self.assertEqual(newjob['chunk_size'],288);self.assertEqual(newjob['window'],2)
  self.assertEqual(newjob['phase'],'succeeded');self.assertLess(fast,slow*.7)
  self.assertTrue(all(len(base64.b64decode(p['data']))<=288 for p in new.sent if p['op']=='chunk'))
  print(f'Injected-latency bench only: serial={slow:.3f}s window2={fast:.3f}s ratio={slow/fast:.2f}x; NOT a Thread radio benchmark')
 async def test_negative_ack_settles_window_before_abort(self):
  hub,job,_=await self.run_transfer(True,'chunk');self.assertEqual(job['phase'],'failed');self.assertNotIn('commit',[p['op'] for p in hub.sent]);self.assertEqual(hub.inflight,0);self.assertEqual(hub.sent[-1]['op'],'abort')
class WindowValidation(unittest.TestCase):
 def test_advertised_fast_receiver_defaults_to_proven_serial_profile(self):
  cap=dict(device()['hello']['ota'],max_chunk=288,max_window=2)
  plan=m.validate_image(image(),board=BOARD,version=VERSION,capability=cap,current='old')
  self.assertEqual((plan['chunk_size'],plan['window']),(192,1))
 def test_invalid_advertised_window_or_chunk_rejected(self):
  for fields in [{'max_chunk':True},{'max_chunk':512},{'max_window':0},{'max_window':3},{'max_window':True}]:
   cap=dict(device()['hello']['ota'],**fields)
   with self.assertRaises(ValueError):m.validate_image(image(),board=BOARD,version=VERSION,capability=cap,current='old')
if __name__=='__main__':unittest.main()
