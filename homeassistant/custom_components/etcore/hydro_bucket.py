"""Native telemetry for the EC-fixed hydroponics bucket; no dosing commands."""
from __future__ import annotations
from datetime import timedelta
import math
import time
from homeassistant.components.sensor import SensorEntity
from homeassistant.core import callback
from homeassistant.helpers.event import async_track_time_interval
from .const import DOMAIN

DEVICE_CLASS = "sensor.hydro_bucket"
PROFILE = "hydro_bucket_ecfix6_v1"
# Preserve the original entity IDs, names, units and statistics metadata.
READINGS = {
 "cf": ("hydro_bucket_system_cf", "Hydro Bucket System CF", "CF", None, "measurement", "mdi:beaker"),
 "ph": ("hydro_bucket_system_ph", "Hydro Bucket System PH", "PH", None, "measurement", "mdi:flask-empty"),
 "cf_dose_count": ("hydro_bucket_system_cf_dose_count", "Hydro Bucket System CF Dose Count", "count", None, "total_increasing", "mdi:counter"),
 "ph_dose_count": ("hydro_bucket_system_ph_dose_count", "Hydro Bucket System PH Dose Count", "count", None, "total_increasing", "mdi:counter"),
 "temperature": ("hydro_bucket_tank_temperature", "Hydro Bucket tank Temperature", "°C", "temperature", "measurement", "mdi:thermometer-water"),
 "ec": ("hydro_bucket_system_ec", "Hydro Bucket System EC", "mS/cm", None, "measurement", "mdi:beaker-check"),
}

def numeric_reading(key, payload):
    value = payload.get(key)
    if key in ("cf", "ec") and payload.get("ec_valid") is not True:
        return None
    if key == "temperature" and payload.get("raw_valid") is not True:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    if key.endswith("_count") and (value < 0 or int(value) != value):
        return None
    return value


def setup_bucket_sensors(hass, entry, hub, add_entities):
    entities = {}
    @callback
    def message(msg):
        dev_id = str(msg.get("id") or "")
        info = hub.devices.get(dev_id, {})
        if not dev_id or info.get("class") != DEVICE_CLASS or (info.get("hello") or {}).get("profile") != PROFILE:
            return
        if msg.get("type") not in ("hello", "state", "lease"):
            return
        new = []
        for key in READINGS:
            if (dev_id, key) not in entities:
                ent = BucketSensor(hub, dev_id, key, info)
                entities[(dev_id, key)] = ent
                new.append(ent)
        if new:
            add_entities(new)
        for (did, _), ent in entities.items():
            if did == dev_id and ent.boot != info.get("boot"):
                ent.boot = info.get("boot")
                ent.sample_at = None
                ent._attr_native_value = None
                ent.write()
        if msg.get("type") != "state" or not isinstance(msg.get("payload"), dict):
            return
        replay_age = max(0, time.time() - float(info.get("state_ts") or time.time())) if msg.get("_replay") else 0
        now = time.monotonic() - replay_age
        for key in READINGS:
            ent = entities[(dev_id, key)]
            ent._attr_native_value = numeric_reading(key, msg["payload"])
            ent.sample_at = now
            ent.write()
    @callback
    def status(data):
        for (did, _), ent in entities.items():
            if did == data.get("id"):
                ent.write()
    @callback
    def removed(dev_id):
        for identity in list(entities):
            if identity[0] == dev_id:
                entities.pop(identity)
    @callback
    def expire(_now):
        for ent in entities.values():
            if ent.available != ent.last_available:
                ent.write()
    hub.register_listener(message)
    hub.register_status_listener(status)
    hub.register_removed_listener(removed)
    entry.async_on_unload(async_track_time_interval(hass, expire, timedelta(seconds=5)))


class BucketSensor(SensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = False
    _attr_native_value = None
    def __init__(self, hub, dev_id, key, info):
        object_id, name, unit, device_class, state_class, icon = READINGS[key]
        self.hub, self.dev_id, self.key = hub, dev_id, key
        self.boot = info.get("boot")
        self.sample_at = None
        self.last_available = False
        self._attr_unique_id = f"etcore_{dev_id}_bucket_{key}"
        self._attr_suggested_object_id = object_id
        self._attr_name = name
        self._attr_native_unit_of_measurement = unit
        self._attr_device_class = device_class
        self._attr_state_class = state_class
        self._attr_icon = icon
        self._attr_device_info = {"identifiers": {(DOMAIN, dev_id)}, "name": info.get("name") or dev_id,
            "manufacturer": "ElectronicsTech", "model": "Hydroponics Bucket EC/pH", "sw_version": str(info.get("fw") or "")}
    @property
    def available(self):
        return bool((self.hub.devices.get(self.dev_id) or {}).get("online") and self._attr_native_value is not None
            and self.sample_at is not None and time.monotonic() - self.sample_at <= 30)
    def write(self):
        self.last_available = self.available
        if self.hass is not None:
            self.async_write_ha_state()
