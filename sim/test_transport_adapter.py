import asyncio,os,unittest
from harness import Device,make_hub,wait_for
class TransportAdapter(unittest.IsolatedAsyncioTestCase):
 async def asyncSetUp(self):
  self.key=os.urandom(32).hex();self.h=make_hub(asyncio.get_running_loop(),self.key);await self.h.async_start();self.ds=[]
 async def asyncTearDown(self):
  for d in self.ds:d.stop()
  await self.h.async_stop()
 def dev(self,key=True,**env):
  d=Device('adapter','switch.relay','127.0.0.245',self.key if key else None,env={'ETCORE_SIM_ADAPTER':'1',**env});self.ds.append(d);return d
 async def test_adapter_authenticated_reporting_and_command(self):
  d=self.dev();self.assertTrue(await wait_for(lambda:self.h.devices.get('adapter',{}).get('online'),5));self.h.send_command('adapter','switch.relay',{'on':True});self.assertTrue(await wait_for(lambda:self.h.devices['adapter']['stats']['acked']==1,5));self.assertEqual(len(d.commands()),1)
  self.assertNotIn('rssi',self.h.devices['adapter']);self.assertEqual(self.h.devices['adapter']['hello']['transport'],'test-adapter')
 async def test_adapter_requires_encryption(self):
  d=self.dev(key=False);self.assertTrue(await wait_for(lambda:d.proc.poll() is not None,2));self.assertIn('ADAPTER_REJECTED',d.poll_lines());self.assertNotIn('adapter',self.h.devices)
 async def test_sleep_adapter_combination_rejected(self):
  d=self.dev(ETCORE_SIM_SLEEP='5');self.assertTrue(await wait_for(lambda:d.proc.poll() is not None,2));self.assertIn('ADAPTER_REJECTED',d.poll_lines())
 async def test_adapter_start_failure_visible(self):
  d=self.dev(ETCORE_SIM_ADAPTER_FAIL='1');self.assertTrue(await wait_for(lambda:any('ADAPTER_READY 0 FAILURES' in l for l in d.poll_lines()),2));self.assertNotIn('adapter',self.h.devices)
