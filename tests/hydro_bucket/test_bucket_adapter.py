import sys,types,unittest,importlib.util,time
from pathlib import Path
from unittest.mock import patch
for n in ['homeassistant','homeassistant.components','homeassistant.components.sensor','homeassistant.core','homeassistant.helpers','homeassistant.helpers.event']:
 sys.modules[n]=types.ModuleType(n)
class Sensor: hass=None
sys.modules['homeassistant.components.sensor'].SensorEntity=Sensor
sys.modules['homeassistant.core'].callback=lambda f:f
sys.modules['homeassistant.helpers.event'].async_track_time_interval=lambda *a:lambda:None
root=Path(__file__).resolve().parents[2]/'homeassistant/custom_components/etcore'
pkg=types.ModuleType('bucket_test');pkg.__path__=[str(root)];sys.modules['bucket_test']=pkg
from bucket_test import hydro_bucket as b
class Hub:
 def __init__(self):
  self.devices={'bucket':{'class':b.DEVICE_CLASS,'hello':{'profile':b.PROFILE},'boot':'one','online':True}};self.listeners=[];self.status=[];self.removed=[]
 def register_listener(self,f):self.listeners.append(f)
 def register_status_listener(self,f):self.status.append(f)
 def register_removed_listener(self,f):self.removed.append(f)
class Entry:
 def async_on_unload(self,f):pass
class Tests(unittest.TestCase):
 def setUp(self):
  self.h=Hub();self.entities=[];b.setup_bucket_sensors(None,Entry(),self.h,self.entities.extend);self.msg('hello',{})
 def msg(self,t,p,**kw):
  for f in self.h.listeners:f({'id':'bucket','type':t,'payload':p,**kw})
 def by(self,k):return next(x for x in self.entities if x.key==k)
 def test_initial_unknown(self):
  self.assertEqual(len(self.entities),6);self.assertFalse(any(x.available for x in self.entities))
 def test_values_and_metadata(self):
  self.msg('state',{'cf':13.7,'ec':1.37,'ph':5.8,'temperature':28,'ec_valid':True,'raw_valid':True,'cf_dose_count':19,'ph_dose_count':0})
  self.assertTrue(all(e.available for e in self.entities));self.assertEqual(self.by('cf')._attr_native_unit_of_measurement,'CF');self.assertEqual(self.by('cf_dose_count')._attr_state_class,'total_increasing')
 def test_invalid_data_clears_old(self):
  self.msg('state',{'cf':14,'ec_valid':True});self.assertTrue(self.by('cf').available)
  for bad in [None,True,'14',{},float('nan'),float('inf')]:
   self.msg('state',{'cf':bad,'ec_valid':True});self.assertFalse(self.by('cf').available);self.assertIsNone(self.by('cf')._attr_native_value)
 def test_invalid_ec_disables_cf_not_ph(self):
  self.msg('state',{'cf':14,'ec':1.4,'ph':6,'ec_valid':False});self.assertFalse(self.by('cf').available);self.assertFalse(self.by('ec').available);self.assertTrue(self.by('ph').available)
 def test_temperature_raw_gate(self):
  self.msg('state',{'temperature':28,'raw_valid':False});self.assertFalse(self.by('temperature').available)
 def test_count_validation(self):
  for v in [-1,1.5,True,'1']:
   self.msg('state',{'cf_dose_count':v});self.assertFalse(self.by('cf_dose_count').available)
 def test_stale_and_disconnect(self):
  self.msg('state',{'ph':6});e=self.by('ph');e.sample_at-=31;self.assertFalse(e.available)
  self.msg('state',{'ph':6});self.h.devices['bucket']['online']=False;self.assertFalse(e.available)
 def test_replay_not_fresh(self):
  self.h.devices['bucket']['state_ts']=time.time()-40;self.msg('state',{'ph':6},_replay=True);self.assertFalse(self.by('ph').available)
 def test_reboot_clears(self):
  self.msg('state',{'ph':6});self.h.devices['bucket']['boot']='two';self.msg('lease',{});self.assertFalse(self.by('ph').available);self.assertIsNone(self.by('ph')._attr_native_value)
 def test_profile_gates(self):
  self.h.devices['bucket']['hello']['profile']='wrong';self.msg('state',{'ph':6});self.assertIsNone(self.by('ph')._attr_native_value)
 def test_removed_recreates(self):
  for f in self.h.removed:f('bucket')
  self.entities.clear();self.msg('hello',{});self.assertEqual(len(self.entities),6)
 def test_lease_does_not_refresh_readings(self):
  self.msg('state',{'ph':6});e=self.by('ph');e.sample_at-=31;self.msg('lease',{});self.assertFalse(e.available)
if __name__=='__main__':unittest.main()
