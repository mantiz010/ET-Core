import importlib.util,sys,types,pathlib,unittest,time,asyncio
from unittest.mock import patch
root=pathlib.Path(__file__).resolve().parents[2]/'homeassistant/custom_components/etcore'
pkg=types.ModuleType('et_bridge');pkg.__path__=[str(root)];sys.modules['et_bridge']=pkg
from et_bridge import bridge as b
from et_bridge.bridge_profiles import CHANNELS,PROFILE,PROFILE_HASH
class Hub:
 def __init__(self):
  self.devices={'test':{'class':'bridge.espnow','name':'Gateway','boot':'one','online':True,'hello':{'profile':PROFILE,'profile_hash':PROFILE_HASH}}};self.listeners=[];self.status=[];self.removed=[];self.sent=[]
 def register_listener(self,f):self.listeners.append(f)
 def register_status_listener(self,f):self.status.append(f)
 def register_removed_listener(self,f):self.removed.append(f)
 def send_command(self,*args):self.sent.append(args)
class Entry:
 def async_on_unload(self,f):pass
class Tests(unittest.IsolatedAsyncioTestCase):
 def setUp(self):
  self.h=Hub();self.entities=[]
  with patch.object(b,'async_track_time_interval',return_value=lambda:None):
   for platform in ['sensor','switch','light','binary_sensor']:b.setup_bridge_platform(None,Entry(),self.h,self.entities.extend,platform)
  self.message('hello',{})
 def message(self,kind,payload,**extra):
  for f in self.h.listeners:f({'id':'test','class':'bridge.espnow','type':kind,'payload':payload,**extra})
 def by(self,key):return next(e for e in self.entities if e.key==key)
 def test_complete_profile_and_unknown_until_observed(self):
  self.assertEqual(len(self.entities),51)
  for e in self.entities:
   if e.desc['platform'] in ['sensor','binary_sensor']:self.assertFalse(e.available)
   else:self.assertTrue(e.available);self.assertIsNone(e.is_on)
 def test_sparse_messages_preserve_other_readings(self):
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}})
  self.message('state',{'values':{'lounge_temperature':22.5},'age_ms':{'lounge_temperature':0}})
  self.assertEqual(self.by('garden_solar').native_value,12.8);self.assertTrue(self.by('garden_solar').available)
  self.assertEqual(self.by('lounge_temperature').native_value,22.5)
 def test_bad_values_do_not_override_last_valid(self):
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}})
  for value in [None,'12.3',float('nan'),float('inf'),True,{},[]]:self.message('state',{'values':{'garden_solar':value},'age_ms':{'garden_solar':0}})
  self.assertEqual(self.by('garden_solar').native_value,12.8)
 def test_expiry_and_hub_offline(self):
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':1801000}});self.assertFalse(self.by('garden_solar').available)
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}});self.h.devices['test']['online']=False
  self.assertFalse(self.by('garden_solar').available);self.assertFalse(self.by('rgb2').available)
 def test_replayed_old_state_is_not_fresh(self):
  self.h.devices['test']['state_ts']=time.time()-2000
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}},_replay=True)
  self.assertFalse(self.by('garden_solar').available)
 def test_new_boot_clears_previous_samples(self):
  self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}})
  self.h.devices['test']['boot']='two';self.message('hello',{})
  self.assertIsNone(self.by('garden_solar').native_value);self.assertFalse(self.by('garden_solar').available)
 def test_wrong_profile_rejected(self):
  self.h.devices['test']['hello']['profile_hash']='wrong';self.message('state',{'values':{'garden_solar':12.8},'age_ms':{'garden_solar':0}})
  self.assertIsNone(self.by('garden_solar').native_value)
 def test_removed_device_recreated(self):
  for f in self.h.removed:f('test')
  self.entities.clear();self.message('hello',{});self.assertEqual(len(self.entities),51)
 async def test_switch_native_command(self):
  e=self.by('garden_relaystate');await e.async_turn_on();self.assertEqual(self.h.sent[-1],('test','bridge.espnow',{'command':'relay_set','on':True}));self.assertIsNone(e.is_on)
  await e.async_turn_off();self.assertEqual(self.h.sent[-1][2]['on'],False)
 async def test_light_partial_command_keeps_fields_unspecified(self):
  e=self.by('rgb2');await e.async_turn_on(brightness=40)
  self.assertEqual(self.h.sent[-1][2],{'command':'rgb12_set','state':'ON','brightness':40})
  await e.async_turn_off();self.assertEqual(self.h.sent[-1][2],{'command':'rgb12_set','state':'OFF'})
 def test_light_off_retains_color(self):
  self.message('state',{'values':{'rgb2':{'state':'OFF','brightness':40,'color':{'r':1,'g':2,'b':3}}},'age_ms':{'rgb2':0}})
  e=self.by('rgb2');self.assertFalse(e.is_on);self.assertEqual(e.rgb_color,(1,2,3));self.assertEqual(e.brightness,40)
 def test_all_platform_fields_validate(self):
  for key,d in CHANNELS.items():
   v={'state':'ON','brightness':40,'color':{'r':1,'g':2,'b':3}} if d['platform']=='light' else True if d['platform'] in ['switch','binary_sensor'] else 12.3
   self.assertTrue(b.valid_value(d,v),key)
class Rf24Tests(Tests):
 def setUp(self):
  self.h=Hub();self.entities=[]
  self.h.devices['test'].update({'class':'bridge.rf24','hello':{'profile':b.rf24_profiles.PROFILE,'profile_hash':b.rf24_profiles.PROFILE_HASH}})
  with patch.object(b,'async_track_time_interval',return_value=lambda:None):
   for platform in ['sensor','switch']:b.setup_bridge_platform(None,Entry(),self.h,self.entities.extend,platform)
  self.message('hello',{})
 def test_rf24_schema(self):
  self.assertEqual(len(self.entities),34)
  self.assertEqual(sum(e.desc['platform']=='sensor' for e in self.entities),33)
  self.assertFalse(any(e.key=='node4_relay_state' for e in self.entities))
 def test_rf24_boot_unknown_and_sparse(self):
  e=self.by('node1_temperature');self.assertFalse(e.available);self.assertIsNone(e.native_value)
  self.message('state',{'values':{'node1_temperature':21.5},'age_ms':{'node1_temperature':0}})
  self.assertTrue(e.available);self.assertEqual(e.native_value,21.5);self.assertFalse(self.by('node2_lux').available)
 async def test_rf24_command(self):
  e=self.by('node1_switch_state');await e.async_turn_on()
  self.assertEqual(self.h.sent[-1],('test','bridge.rf24',{'command':'rf24_switch_set','on':True}));self.assertIsNone(e.is_on)
 def test_rf24_expiry(self):
  self.message('state',{'values':{'node2_lux':200},'age_ms':{'node2_lux':1801000}});self.assertFalse(self.by('node2_lux').available)
 def test_rf24_hash_rejects(self):
  self.h.devices['test']['hello']['profile_hash']='wrong';self.message('state',{'values':{'node1_temperature':21},'age_ms':{'node1_temperature':0}});self.assertIsNone(self.by('node1_temperature').native_value)
# Inherit the helper methods, not ESP-NOW-specific test cases.
for name in list(Tests.__dict__):
 if name.startswith('test_') and name not in Rf24Tests.__dict__:setattr(Rf24Tests,name,None)
if __name__=='__main__':unittest.main()

