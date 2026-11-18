import asyncio,base64,copy,hashlib,importlib,json,struct,sys,types,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]/'homeassistant/custom_components/etcore'
pkg=types.ModuleType('thread_ota_tests');pkg.__path__=[str(R)];sys.modules[pkg.__name__]=pkg
m=importlib.import_module(pkg.__name__+'.thread_ota')
BOARD='esp32c6-dev-4m';VERSION='thread-preview-0.4'
def image():
 header=bytearray(24);header[0]=0xE9;header[1]=1;header[3]=0x20;header[12]=13;header[23]=1
 data=bytearray(256);struct.pack_into('<I',data,0,0xABCD5432)
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
 async def test_restored_active_jobs_are_interrupted(self):
  self.manager.restore({'bench':{'phase':'transferring','progress':20}})
  self.assertEqual(self.manager.jobs['bench']['phase'],'interrupted');self.assertEqual(self.hub.devices['bench']['ota_job']['phase'],'interrupted');self.assertEqual(self.hub.sent,[])
 async def test_stale_counter_reply_waits_for_hub_resend(self):
  f=self.hub.hass.loop.create_future();self.manager.waiters['pending']=('bench',f)
  self.manager.receive({'_encrypted':True,'type':'ack','id':'bench','payload':{'cmd_id':'pending','ok':False,'err':'stale'}})
  self.assertFalse(f.done())
if __name__=='__main__':unittest.main()
