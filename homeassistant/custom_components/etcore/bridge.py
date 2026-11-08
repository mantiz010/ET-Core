"""Native entities for the authenticated ESP-NOW UART bridge profile.

One encrypted device, several native HA domains. A command ACK is gateway UART
acceptance; it is not a remote node or physical relay acknowledgement.
"""
from __future__ import annotations
from datetime import timedelta
import math
import time
from homeassistant.components.sensor import SensorEntity
from homeassistant.components.switch import SwitchEntity
from homeassistant.components.binary_sensor import BinarySensorEntity
from homeassistant.components.light import LightEntity, ColorMode, ATTR_BRIGHTNESS, ATTR_RGB_COLOR, ATTR_TRANSITION, LightEntityFeature
from homeassistant.core import callback
from homeassistant.helpers.event import async_track_time_interval
from .const import DOMAIN
from .bridge_profiles import PROFILE, PROFILE_HASH, CHANNELS


def valid_profile(info):
    hello=info.get('hello') or {}
    return info.get('class')=='bridge.espnow' and hello.get('profile')==PROFILE and hello.get('profile_hash')==PROFILE_HASH


def valid_value(desc,value):
    platform=desc['platform']
    if platform=='sensor':return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)
    if platform in ('switch','binary_sensor'):return isinstance(value,bool) or type(value) is int and value in (0,1) or type(value) is float and value in (0.,1.)
    if platform=='light':
        if not isinstance(value,dict) or value.get('state') not in ('ON','OFF'):return False
        color=value.get('color')
        return isinstance(color,dict) and all(type(color.get(c)) is int and 0<=color[c]<=255 for c in ('r','g','b')) and type(value.get('brightness')) is int and 0<=value['brightness']<=255
    return False


def setup_bridge_platform(hass,entry,hub,add_entities,platform):
    entities={}
    classes={'sensor':BridgeSensor,'switch':BridgeSwitch,'binary_sensor':BridgeBinary,'light':BridgeLight}
    @callback
    def message(msg):
        dev_id=str(msg.get('id') or '');info=hub.devices.get(dev_id,{})
        if not dev_id or not valid_profile(info) or msg.get('type') not in ('hello','state','lease'):return
        new=[]
        for key,desc in CHANNELS.items():
            if desc['platform']!=platform:continue
            identity=(dev_id,key)
            if identity not in entities:
                ent=classes[platform](hub,dev_id,key,desc,info);entities[identity]=ent;new.append(ent)
        if new:add_entities(new)
        for (did,_),ent in entities.items():
            if did==dev_id and ent.boot!=info.get('boot'):
                ent.boot=info.get('boot');ent.sample_at=None
                if ent.desc['platform']=='sensor':ent._attr_native_value=None
                else:ent._attr_is_on=None
                ent.write()
        if msg.get('type')!='state':return
        payload=msg.get('payload') or {};values=payload.get('values');ages=payload.get('age_ms')
        if not isinstance(values,dict) or not isinstance(ages,dict):return
        now=time.monotonic()
        replay_age=max(0,time.time()-float(info.get("state_ts") or time.time())) if msg.get("_replay") else 0
        for key,value in values.items():
            ent=entities.get((dev_id,key));age=ages.get(key)
            if ent is None or type(age) is not int or not 0<=age<=0xffffffff or not valid_value(ent.desc,value):continue
            ent.apply(value,now-age/1000-replay_age)
    @callback
    def status(data):
        for (dev_id,_),ent in entities.items():
            if data.get('id')==dev_id:ent.write()
    @callback
    def removed(dev_id):
        for key in list(entities):
            if key[0]==dev_id:entities.pop(key)
    @callback
    def expire(_now):
        for ent in entities.values():
            available=ent.available
            if available!=ent._last_available:ent._last_available=available;ent.write()
    hub.register_listener(message);hub.register_status_listener(status);hub.register_removed_listener(removed)
    entry.async_on_unload(async_track_time_interval(hass,expire,timedelta(seconds=30)))


class BridgeEntity:
    _attr_should_poll=False
    _attr_has_entity_name=True
    def __init__(self,hub,dev_id,key,desc,info):
        self.hub=hub;self.dev_id=dev_id;self.key=key;self.desc=desc;self.sample_at=None;self._last_available=False;self.boot=info.get("boot")
        self._attr_unique_id=f'etcore_{dev_id}_bridge_{key}'
        self._attr_name=desc['name'];self._attr_icon=desc.get('icon');self._attr_device_class=desc.get('device_class')
        self._attr_device_info={'identifiers':{(DOMAIN,dev_id)},'name':info.get('name') or dev_id,'manufacturer':'ElectronicsTech','model':'ESP32 ESP-NOW UART/E220 Gateway','sw_version':str(info.get('fw') or '')}
    @property
    def available(self):
        if not (self.hub.devices.get(self.dev_id) or {}).get('online',False):return False
        # A control can accept a first explicit command even before an echo exists.
        if self.desc['platform'] in ('switch','light'):return True
        return self.sample_at is not None and time.monotonic()-self.sample_at<=self.desc['ttl']
    @property
    def extra_state_attributes(self):
        if self.desc['platform'] in ('switch','light'):return {'state_confirmation':'gateway command echo; remote actuation unconfirmed'}
        return None
    def write(self):
        if self.hass is not None:self.async_write_ha_state()
    def send(self,payload):self.hub.send_command(self.dev_id,'bridge.espnow',{'command':self.desc['command'],**payload})
    def apply(self,value,when):
        self.sample_at=when
        if self.desc['platform']=='sensor':self._attr_native_value=value
        elif self.desc['platform'] in ('switch','binary_sensor'):self._attr_is_on=bool(value)
        else:
            self._attr_is_on=value['state']=='ON';self._attr_brightness=value['brightness'];self._attr_rgb_color=tuple(value['color'][c] for c in ('r','g','b'))
        self.write()


class BridgeSensor(BridgeEntity,SensorEntity):
    _attr_native_value=None
    def __init__(self,*args):
        super().__init__(*args);self._attr_native_unit_of_measurement=self.desc.get('unit')
        # Preserve the legacy state-class metadata; do not silently create statistics.
        self._attr_state_class=self.desc.get('state_class')


class BridgeBinary(BridgeEntity,BinarySensorEntity):
    _attr_is_on=None


class BridgeSwitch(BridgeEntity,SwitchEntity):
    _attr_is_on=None
    async def async_turn_on(self,**kwargs):self.send({'on':True})
    async def async_turn_off(self,**kwargs):self.send({'on':False})


class BridgeLight(BridgeEntity,LightEntity):
    _attr_is_on=None
    _attr_brightness=None
    _attr_rgb_color=None
    _attr_color_mode=ColorMode.RGB
    _attr_supported_color_modes={ColorMode.RGB}
    _attr_supported_features=LightEntityFeature.TRANSITION
    async def async_turn_on(self,**kwargs):
        p={'state':'ON'}
        if ATTR_BRIGHTNESS in kwargs:p['brightness']=int(kwargs[ATTR_BRIGHTNESS])
        if ATTR_RGB_COLOR in kwargs:p['color']=dict(zip(('r','g','b'),map(int,kwargs[ATTR_RGB_COLOR])))
        if ATTR_TRANSITION in kwargs:
            transition=float(kwargs[ATTR_TRANSITION])
            if not math.isfinite(transition) or transition<0 or transition>2147483647:raise ValueError('Invalid transition')
            p['transition']=int(transition)
        self.send(p)
    async def async_turn_off(self,**kwargs):
        p={'state':'OFF'}
        if ATTR_TRANSITION in kwargs:
            transition=float(kwargs[ATTR_TRANSITION])
            if not math.isfinite(transition) or transition<0 or transition>2147483647:raise ValueError('Invalid transition')
            p['transition']=int(transition)
        self.send(p)
