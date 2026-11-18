import asyncio,importlib,json,sys,types,unittest
from aiohttp import web
from aiohttp.test_utils import TestClient,TestServer
from sim.test_thread_ota import Hub,BOARD,VERSION,image,m
http=types.ModuleType('homeassistant.components.http');http.HomeAssistantView=object;http.KEY_HASS_USER='hass_user';sys.modules.setdefault('homeassistant',types.ModuleType('homeassistant'));sys.modules.setdefault('homeassistant.components',types.ModuleType('homeassistant.components'));sys.modules['homeassistant.components.http']=http
view=importlib.import_module('thread_ota_tests.thread_ota_view')
class Views(unittest.IsolatedAsyncioTestCase):
 async def asyncSetUp(self):
  self.hub=Hub();self.hub.thread_ota=m.ThreadOTA(self.hub);self.addAsyncCleanup(self.hub.thread_ota.close);self.admin=True;self.user=True
  hass=types.SimpleNamespace(data={'etcore':{'entry':self.hub}})
  @web.middleware
  async def auth(req,handler):
   if self.user:req['hass_user']=types.SimpleNamespace(id='admin',is_admin=self.admin)
   return await handler(req)
  app=web.Application(middlewares=[auth],client_max_size=view.MAX_IMAGE+65536);app['hass']=hass;v=view.ThreadOTAView()
  async def post(req):return await v.post(req,**req.match_info)
  app.router.add_post('/etcore/api/thread_ota/{entry_id}/{dev_id}/{action}',post)
  self.client=TestClient(TestServer(app));await self.client.start_server();self.addAsyncCleanup(self.client.close)
 async def prepare(self):return await self.client.post('/etcore/api/thread_ota/entry/bench/prepare?board='+BOARD+'&version='+VERSION,data=image(),headers={'Content-Type':'application/octet-stream'})
 async def test_prepare_review_has_no_commands(self):
  response=await self.prepare();self.assertEqual(response.status,200);self.assertEqual(self.hub.sent,[]);self.assertIn('sha256',await response.json())
 async def test_admin_required(self):
  for admin,user in [(False,True),(True,False)]:
   self.admin=admin;self.user=user;response=await self.prepare();self.assertEqual(response.status,403)
  self.assertEqual(self.hub.sent,[])
 async def test_invalid_bin_and_wrong_type_rejected(self):
  r=await self.client.post('/etcore/api/thread_ota/entry/bench/prepare',data=b'bad',headers={'Content-Type':'application/octet-stream'});self.assertEqual(r.status,400)
  r=await self.client.post('/etcore/api/thread_ota/entry/bench/prepare',json={});self.assertEqual(r.status,400);self.assertEqual(self.hub.sent,[])
 async def test_explicit_confirmation_required(self):
  p=await(await self.prepare()).json();r=await self.client.post('/etcore/api/thread_ota/entry/bench/install',json={'plan_id':p['plan_id'],'sha256':p['sha256']});self.assertEqual(r.status,400);self.assertEqual(self.hub.sent,[])
 async def test_review_and_install(self):
  p=await(await self.prepare()).json();r=await self.client.post('/etcore/api/thread_ota/entry/bench/install',json={'plan_id':p['plan_id'],'sha256':p['sha256'],'confirm':True});self.assertEqual(r.status,202);await self.hub.thread_ota.tasks['bench'];self.assertEqual(self.hub.thread_ota.jobs['bench']['phase'],'succeeded')
 async def test_confirmation_size_bound(self):
  r=await self.client.post('/etcore/api/thread_ota/entry/bench/install',data=b'x'*2048,headers={'Content-Type':'application/json'});self.assertEqual(r.status,400)
 async def test_unknown_hub_or_device(self):
  for url in ['unknown/bench','entry/unknown']:
   r=await self.client.post('/etcore/api/thread_ota/'+url+'/prepare',data=image());self.assertEqual(r.status,404)
if __name__=='__main__':unittest.main()
