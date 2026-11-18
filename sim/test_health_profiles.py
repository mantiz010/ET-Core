import ast,asyncio,importlib.util,types,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]/'homeassistant/custom_components/etcore'
spec=importlib.util.spec_from_file_location('health_profiles',ROOT/'health_profiles.py');model=importlib.util.module_from_spec(spec);spec.loader.exec_module(model)

class Profiles(unittest.TestCase):
    def test_thread_metadata_suppresses_wifi_signal(self):
        p=model.describe_device({'hello':{'transport':'thread'},'rssi':-90})
        self.assertEqual(p['kind'],'thread_device');self.assertEqual(p['transport'],'thread')
        self.assertFalse(p['signal_valid']);self.assertFalse(p['signal_applicable']);self.assertIn('border router',p['check'])
    def test_sleeping_thread_keeps_sleep_schedule(self):
        p=model.describe_device({'hello':{'transport':'thread'},'sleep_interval':60})
        self.assertEqual(p['kind'],'sleeping_sensor');self.assertTrue(p['sleep_schedule_known'])

    def test_zero_signal_does_not_mean_wifi_or_ethernet(self):
        p=model.describe_device({'rssi':0});self.assertEqual(p['kind'],'unknown');self.assertFalse(p['signal_valid'])
    def test_negative_signal_is_inferred_and_labelled(self):
        p=model.describe_device({'rssi':-67});self.assertEqual(p['kind'],'wifi_device');self.assertEqual(p['source'],'reported_signal');self.assertTrue(p['signal_valid'])
    def test_wired_service_has_no_radio_advice_even_with_bad_rssi(self):
        p=model.describe_device({'rssi':-95},'wired_service');self.assertFalse(p['signal_applicable']);self.assertIn('service restarts',p['check']);self.assertNotIn('Wi-Fi',p['check'])
    def test_sleep_profile_uses_schedule_without_mutating_availability(self):
        d={'sleep_interval':60,'online':False};before=dict(d);p=model.describe_device(d);self.assertEqual(p['kind'],'sleeping_sensor');self.assertTrue(p['sleep_schedule_known']);self.assertEqual(d,before)
    def test_selected_sleep_type_does_not_invent_schedule(self):
        p=model.describe_device({},'sleeping_sensor');self.assertFalse(p['sleep_schedule_known']);self.assertEqual(p['unavailable_label'],'Sensor heartbeat missing')
    def test_reported_service_metadata(self):
        p=model.describe_device({'hello':{'device_kind':'service','transport':'ethernet'},'rssi':0});self.assertEqual(p['kind'],'wired_service');self.assertEqual(p['source'],'device_metadata')
    def test_unknown_override_suppresses_signal_inference(self):
        p=model.describe_device({'rssi':-60},'unknown');self.assertEqual(p['transport'],'unknown');self.assertFalse(p['signal_valid'])
    def test_invalid_signals_and_intervals_are_not_evidence(self):
        for x in [True,None,'-60',float('nan'),float('inf'),10**1000,0,1,-128]:
            self.assertFalse(model.valid_signal(x))
        for x in [True,'60',float('inf'),10**1000,-1]:
            self.assertFalse(model.describe_device({'sleep_interval':x})['sleep_schedule_known'])
    def test_unknown_service_does_not_get_wifi_advice(self):
        p=model.describe_device({'class':'service.compute','rssi':0});self.assertEqual(p['kind'],'service');self.assertFalse(p['signal_valid'])

class ProfileStorage(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.saved={};self.fail=False;owner=self
        class Store:
            def __init__(self,*a):pass
            async def async_load(self):return owner.saved
            async def async_save(self,data):
                if owner.fail:raise OSError('test disk error')
                owner.saved=data
        tree=ast.parse((ROOT/'panel.py').read_text());names={'_profile_state','_load_profiles','_ws_set_health_profile'}
        fns=[n for n in tree.body if isinstance(n,(ast.FunctionDef,ast.AsyncFunctionDef)) and n.name in names]
        for f in fns:f.decorator_list=[]
        ns={'asyncio':asyncio,'DOMAIN':'etcore','Store':Store,'HomeAssistant':object,'Any':object,'websocket_api':types.SimpleNamespace(ActiveConnection=object),'PROFILES':model.PROFILES,'_hubs':lambda h:h.data.get('etcore',{}),'_LOGGER':types.SimpleNamespace(exception=lambda *a:None)}
        exec(compile(ast.Module(body=fns,type_ignores=[]),'profile-storage','exec'),ns);self.ns=ns
        self.hass=types.SimpleNamespace(data={'etcore':{'a':types.SimpleNamespace(devices={'same':{}}),'b':types.SimpleNamespace(devices={'same':{}})}})
        self.responses=[];self.conn=types.SimpleNamespace(send_error=lambda *a:self.responses.append(('error',a)),send_result=lambda *a:self.responses.append(('ok',a)))
    async def save(self,entry='a',profile='wired_service',dev='same'):
        await self.ns['_ws_set_health_profile'](self.hass,self.conn,{'id':1,'entry_id':entry,'dev_id':dev,'profile':profile})
    async def test_scoped_by_connection_and_persists(self):
        await self.save();await self.save('b','wifi_device');self.assertEqual(self.saved,{'a':{'same':'wired_service'},'b':{'same':'wifi_device'}})
        del self.hass.data['etcore_health_profiles'];await self.ns['_load_profiles'](self.hass);self.assertEqual(self.ns['_profile_state'](self.hass)['data'],self.saved)
    async def test_auto_removes_only_selected_override(self):
        await self.save();await self.save('b','wifi_device');await self.save(profile='auto');self.assertEqual(self.saved['a'],{});self.assertEqual(self.saved['b'],{'same':'wifi_device'})
    async def test_unknown_device_rejected_without_write(self):
        await self.save(dev='missing');self.assertEqual(self.saved,{});self.assertEqual(self.responses[-1][0],'error')
    async def test_disk_failure_does_not_claim_success_or_change_profile(self):
        await self.save();self.fail=True;await self.save(profile='wifi_device');self.assertEqual(self.responses[-1][0],'error');self.assertEqual(self.ns['_profile_state'](self.hass)['data']['a']['same'],'wired_service')
    async def test_invalid_stored_values_filtered(self):
        self.saved={'a':{'same':[],'other':'wifi_device'},'bad':2};await self.ns['_load_profiles'](self.hass);self.assertEqual(self.ns['_profile_state'](self.hass)['data'],{'a':{'other':'wifi_device'}})
