import ast, types, unittest, re
import voluptuous as vol
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
R=Path(__file__).resolve().parents[1]/'homeassistant/custom_components/etcore'
mod=ast.parse((R/'panel.py').read_text());fn=next(n for n in mod.body if isinstance(n,ast.FunctionDef) and n.name=='_ws_thread_bootstrap');fn.decorator_list=[]
ns={'HomeAssistant':object,'websocket_api':types.SimpleNamespace(ActiveConnection=object),'Any':object,'DOMAIN':'etcore'}
exec(compile(ast.Module(body=[fn],type_ignores=[]),'bootstrap','exec'),ns)
class Connection:
 def __init__(self):self.error=None;self.result=None
 def send_error(self,mid,code,message):self.error=code
 def send_result(self,mid,result):self.result=result
class Hub:
 _psk=b'key';_enable_ipv6=True
 def __init__(self):self.sent=[]
 def _make_challenge(self,*a):return 'challenge'
 def _send_announce(self,*a,**kw):self.sent.append((a,kw))
class Tests(unittest.TestCase):
 def setUp(self):
  self.hub=Hub();self.hass=types.SimpleNamespace(data={'etcore':{'test':self.hub}});self.conn=Connection();self.msg={'id':1,'entry_id':'test','dev_id':'node','session':'ab'*7,'address':'fd12::5'}
 def call(self):ns['_ws_thread_bootstrap'](self.hass,self.conn,self.msg)
 def test_challenge_requires_normal_proof(self):
  self.call();self.assertTrue(self.conn.result);self.assertEqual(len(self.hub.sent),1);self.assertEqual(self.hub.sent[0][1]['ds'],'ab'*7)
 def test_rate_limit(self):
  self.call();self.call();self.assertEqual(self.conn.error,'rate_limited');self.assertEqual(len(self.hub.sent),1)
 def test_non_unicast_rejected(self):
  for ip in ['::','::1','ff02::1','fe80::1','127.0.0.1','bad']:
   self.msg['address']=ip;self.call();self.assertEqual(self.conn.error,'invalid_address')
 def test_unencrypted_or_ipv4_hub_rejected(self):
  self.hub._psk=None;self.call();self.assertEqual(self.conn.error,'invalid_hub')
  self.hub._psk=b'key';self.hub._enable_ipv6=False;self.call();self.assertEqual(self.conn.error,'invalid_hub')
class ConfigTests(unittest.TestCase):
 def setUp(self):
  mod=ast.parse((R/'config_flow.py').read_text());nodes=[n for n in mod.body if isinstance(n,ast.FunctionDef) and n.name in ('_clean_psk','_schema','_validate')]
  self.ns={'vol':vol,'re':re,'CONF_PSK_HEX':'psk_hex','CONF_PORT':'port','PORT':5560}
  exec(compile(ast.Module(body=nodes,type_ignores=[]),'flow','exec'),self.ns)
 def test_ipv4_default_unchanged(self):
  self.assertFalse(self.ns['_schema']('',5560)({'port':5560})['enable_ipv6'])
 def test_ipv6_requires_key(self):
  self.assertTrue(self.ns['_validate']({'port':15562,'enable_ipv6':True})[1])
  self.assertTrue(self.ns['_validate']({'port':15562,'enable_ipv6':True,'psk_hex':'ab'*32})[0]['enable_ipv6'])
if __name__=='__main__':unittest.main()
