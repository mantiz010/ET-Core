from __future__ import annotations

from typing import Any

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub

# binary_sensor.<kind>: {"on": true} means motion / open / wet / ...
_KINDS: dict[str, BinarySensorDeviceClass | None] = {
    "motion": BinarySensorDeviceClass.MOTION,
    "occupancy": BinarySensorDeviceClass.OCCUPANCY,
    "presence": BinarySensorDeviceClass.PRESENCE,
    "door": BinarySensorDeviceClass.DOOR,
    "window": BinarySensorDeviceClass.WINDOW,
    "garage_door": BinarySensorDeviceClass.GARAGE_DOOR,
    "opening": BinarySensorDeviceClass.OPENING,
    "moisture": BinarySensorDeviceClass.MOISTURE,
    "leak": BinarySensorDeviceClass.MOISTURE,
    "smoke": BinarySensorDeviceClass.SMOKE,
    "gas": BinarySensorDeviceClass.GAS,
    "vibration": BinarySensorDeviceClass.VIBRATION,
    "sound": BinarySensorDeviceClass.SOUND,
    "light": BinarySensorDeviceClass.LIGHT,
    "power": BinarySensorDeviceClass.POWER,
    "problem": BinarySensorDeviceClass.PROBLEM,
    "button": None,
    "contact": BinarySensorDeviceClass.OPENING,
}


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    """binary_sensor.* devices: motion, door, leak... one on/off reading."""
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtCoreBinarySensor] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("binary_sensor.") or msg.get("type") not in ("hello", "lease", "state"):
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return
        if dev_id not in entities:
            entities[dev_id] = EtCoreBinarySensor(hub, dev_id, dev_class, info)
            async_add_entities([entities[dev_id]])
        ent = entities[dev_id]
        if msg.get("type") == "state" and "on" in payload:
            ent.set_on(bool(payload["on"]))
        elif ent.hass is not None:
            ent.async_write_ha_state()

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        ent = entities.get(str(data.get("id") or ""))
        if ent and ent.hass is not None:
            ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)

    @callback
    def handle_removed(dev_id: str) -> None:
        entities.pop(dev_id, None)

    hub.register_removed_listener(handle_removed)


class EtCoreBinarySensor(BinarySensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = None  # the device's main feature: named after the device

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, info: dict[str, Any]):
        self._hub = hub
        self._dev_id = dev_id
        kind = dev_class.split(".", 1)[1] if "." in dev_class else ""
        self._attr_device_class = _KINDS.get(kind)
        self._attr_unique_id = f"etcore_{dev_id}_binary"
        self._attr_is_on = None
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": info.get("name") or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": str((info.get("hello") or {}).get("model") or "ET-Core Sensor"),
            "sw_version": str(info.get("fw") or ""),
        }

    @property
    def available(self) -> bool:
        return bool((self._hub.devices.get(self._dev_id) or {}).get("online", False))

    def set_on(self, on: bool) -> None:
        self._attr_is_on = on
        if self.hass is not None:
            self.async_write_ha_state()
