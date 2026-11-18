"""Real IPv6 loopback datagrams; no Thread radio or production hub required."""
import asyncio,base64,hashlib,json,socket,time,unittest
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from harness import FakeHass,load_hub_module
def unused_port():
 s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close();return port

class IPv6Transport(unittest.IsolatedAsyncioTestCase):
 async def asyncSetUp(self):
  port=unused_port()
  self.key=bytes.fromhex('ab'*32);self.session=b'1234567';self.hub=load_hub_module().EtCoreHub(FakeHass(asyncio.get_running_loop()),{'port':port,'psk_hex':self.key.hex(),'enable_ipv6':True,'ipv6_bind':'::1'})
  self.assertEqual(self.hub._psk,self.key)
  await self.hub.async_start();self.client=socket.socket(socket.AF_INET6,socket.SOCK_DGRAM);self.client.bind(('::1',0));self.client.setblocking(False)
 async def asyncTearDown(self):
  self.client.close();await self.hub.async_stop()
 def packet(self,seq=1,key=None):
  nonce=b'\x01'+self.session+seq.to_bytes(4,'little');k=hashlib.sha256((key or self.key)+b'v6test').digest();out=ChaCha20Poly1305(k).encrypt(nonce,b'{"temperature":21.5}',f'v1|state|v6test|{seq}'.encode());b64=lambda x:base64.b64encode(x).decode()
  return json.dumps({'v':1,'type':'state','id':'v6test','class':'sensor.temperature','seq':seq,'payload':{'_enc':1,'n':b64(nonce),'ct':b64(out[:-16]),'tag':b64(out[-16:])}}).encode()
 def seed(self):self.hub._devices['v6test']={'id':'v6test','rx_session':self.session.hex(),'rx_seq':0,'told_hub_at':time.time(),'name':'Test'}
 async def send(self,data):
  self.client.sendto(data,('::1',self.hub._port));await asyncio.sleep(.08)
 async def test_ipv6_receives_authenticated_state(self):
  self.seed();await self.send(self.packet());d=self.hub.devices['v6test'];self.assertEqual(d['state']['temperature'],21.5);self.assertEqual(d['last_addr'],'::1')
 async def test_replay_rejected_over_ipv6(self):
  self.seed();await self.send(self.packet());await self.send(self.packet());self.assertEqual(self.hub._replay_count,1)
 async def test_bad_key_cannot_update_device(self):
  self.seed();await self.send(self.packet(key=b'c'*32));self.assertNotIn('state',self.hub.devices['v6test'])
 async def test_ipv6_reply_socket_and_route_selection(self):
  self.hub._send_raw(b'reply',self.client.getsockname());data=await asyncio.wait_for(asyncio.get_running_loop().sock_recv(self.client,256),1);self.assertEqual(data,b'reply');self.assertEqual(self.hub._source_ip('::1'),'::1')
 async def test_encrypted_command_and_ack_roundtrip(self):
  self.seed();await self.send(self.packet());original=self.hub._send_raw
  # Two test processes share ::1; redirect only the destination port to the
  # peer's ephemeral socket. Real nodes listen on the hub's configured port.
  self.hub._send_raw=lambda data,addr:original(data,self.client.getsockname() if addr[0]=='::1' else addr)
  self.hub.send_command('v6test','sensor.temperature',{'test_value':42})
  raw=await asyncio.wait_for(asyncio.get_running_loop().sock_recv(self.client,4096),1);m=json.loads(raw);p=m['payload'];key=hashlib.sha256(self.key+b'v6test').digest()
  decode=lambda v:base64.b64decode(v)
  plain=json.loads(ChaCha20Poly1305(key).decrypt(decode(p['n']),decode(p['ct'])+decode(p['tag']),f"v1|cmd|v6test|{m['seq']}".encode()))
  self.assertEqual(plain['test_value'],42)
  seq=2;nonce=b'\x01'+self.session+seq.to_bytes(4,'little');ack=json.dumps({'cmd_id':plain['cmd_id'],'ok':True}).encode();out=ChaCha20Poly1305(key).encrypt(nonce,ack,b'v1|ack|v6test|2');enc=lambda b:base64.b64encode(b).decode()
  msg={'v':1,'type':'ack','id':'v6test','class':'sensor.temperature','seq':seq,'payload':{'_enc':1,'n':enc(nonce),'ct':enc(out[:-16]),'tag':enc(out[-16:])}}
  await self.send(json.dumps(msg).encode());self.assertEqual(self.hub.devices['v6test']['stats']['acked'],1);self.assertEqual(self.hub._pending,{})
 async def test_ipv6_keepalive_is_encrypted_and_session_bound(self):
  self.seed();await self.send(self.packet());self.hub.devices['v6test']['online']=True
  original=self.hub._send_raw
  self.hub._send_raw=lambda data,addr:original(data,self.client.getsockname())
  self.hub._announce_ipv6_peers()
  raw=await asyncio.wait_for(asyncio.get_running_loop().sock_recv(self.client,4096),1)
  msg=json.loads(raw);p=msg['payload'];decode=lambda s:base64.b64decode(s)
  key=hashlib.sha256(self.key+b'v6test').digest()
  plain=json.loads(ChaCha20Poly1305(key).decrypt(decode(p['n']),decode(p['ct'])+decode(p['tag']),f"v1|announce|v6test|{msg['seq']}".encode()))
  self.assertEqual(plain['hub'],'::1');self.assertEqual(plain['_ds'],self.session.hex());self.assertFalse(plain['hello'])
 async def test_ipv6_keepalive_skips_ipv4_sleeping_offline_unproven(self):
  base={'online':True,'last_addr':'::1','rx_session':self.session.hex()}
  self.hub._devices={'v4':dict(base,last_addr='127.0.0.1'),'sleep':dict(base,sleep_interval=60),'offline':dict(base,online=False),'unproven':dict(base,rx_session=None)}
  sent=[];self.hub._send_announce=lambda *a,**k:sent.append((a,k))
  self.hub._announce_ipv6_peers();self.assertEqual(sent,[])
 async def test_keepalive_runs_on_periodic_announce(self):
  # The already-running announcer task must invoke the IPv6 peer sweep too.
  from unittest.mock import patch
  await self.hub.async_stop()
  calls=[]
  self.hub._send_announce=lambda *a,**k:calls.append(('v4',k))
  self.hub._announce_ipv6_peers=lambda:calls.append(('v6',{}))
  count=0
  async def step(_):
   nonlocal count
   count+=1
   if count>1:raise asyncio.CancelledError
  with patch('asyncio.sleep',step):
   with self.assertRaises(asyncio.CancelledError):await self.hub._announcer()
  self.assertEqual([k for k,v in calls],['v4','v4','v6'])
 async def test_firmware_chunk_fits_thread_datagram_cap(self):
  self.seed();self.hub._devices['v6test']['last_addr']='::1';sent=[]
  self.hub._send_raw=lambda data,addr:sent.append(data)
  self.hub.send_command('v6test','firmware.ota',{'op':'chunk','transfer':'ab'*16,'offset':1200000,'data':base64.b64encode(b'x'*288).decode()})
  self.assertEqual(len(sent),1);self.assertLessEqual(len(sent[0]),1024)
 async def test_largest_supported_id_and_chunk_fit_cap(self):
  dev_id='x'*64;self.hub._devices[dev_id]={'id':dev_id,'last_addr':'::1','rx_session':self.session.hex()};self.hub._seq=4294967000;sent=[]
  self.hub._send_raw=lambda data,addr:sent.append(data)
  self.hub.send_command(dev_id,'firmware.ota',{'op':'chunk','transfer':'ab'*16,'offset':4294967295,'data':base64.b64encode(b'x'*288).decode()})
  self.assertEqual(len(sent),1);self.assertLessEqual(len(sent[0]),1024)
 async def test_shutdown_closes_both_sockets(self):
  v4=self.hub._transport;v6=self.hub._transport6;await self.hub.async_stop();self.assertTrue(v4.is_closing());self.assertTrue(v6.is_closing());self.assertIsNone(self.hub._transport6)
 async def test_ipv4_still_receives_state_with_ipv6_enabled(self):
  self.seed();s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.sendto(self.packet(),('127.0.0.1',self.hub._port));s.close();await asyncio.sleep(.08);self.assertEqual(self.hub.devices['v6test']['last_addr'],'127.0.0.1')

class IPv6Lifecycle(unittest.IsolatedAsyncioTestCase):
 async def test_ipv6_disabled_by_default(self):
  h=load_hub_module().EtCoreHub(FakeHass(asyncio.get_running_loop()),{'port':unused_port()})
  try:
   await h.async_start();self.assertIsNone(h._transport6);self.assertIsNotNone(h._transport)
  finally:await h.async_stop()
 async def test_ipv6_start_failure_closes_ipv4_socket(self):
  h=load_hub_module().EtCoreHub(FakeHass(asyncio.get_running_loop()),{'port':unused_port(),'enable_ipv6':True,'ipv6_bind':'not-an-address'})
  with self.assertRaises(OSError):await h.async_start()
  self.assertIsNone(h._transport);self.assertIsNone(h._transport6);self.assertEqual(h._tasks,[])
