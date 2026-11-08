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
    from .bridge import setup_bridge_platform
    setup_bridge_platform(hass, entry, hub, async_add_entities, 'binary_sensor')
    entities: dict[tuple[str, str], EtCoreBinarySensor] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or msg.get("type") not in ("hello", "lease", "state"):
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return
        # A multi-reading lightning device carries its binary alarm alongside
        # numeric counters, on the same encrypted state packet and device ID.
        if dev_class == "sensor.lightning":
            if msg.get("type") != "state" or "lightning_detected" not in payload or (payload["lightning_detected"] is not None and not isinstance(payload["lightning_detected"], bool)):
                return
            key = (dev_id, "lightning_detected")
            if key not in entities:
                entities[key] = EtCoreBinarySensor(hub, dev_id, "binary_sensor.lightning", info,
                                                  metric="lightning_detected", name="Lightning detected",
                                                  icon="mdi:weather-lightning")
                async_add_entities([entities[key]])
            entities[key].set_on(payload["lightning_detected"])
            return
        if not dev_class.startswith("binary_sensor."):
            return
        key = (dev_id, "binary")
        if key not in entities:
            entities[key] = EtCoreBinarySensor(hub, dev_id, dev_class, info)
            async_add_entities([entities[key]])
        ent = entities[key]
        if msg.get("type") == "state" and "on" in payload:
            ent.set_on(bool(payload["on"]))
        elif ent.hass is not None:
            ent.async_write_ha_state()

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        dev_id = str(data.get("id") or "")
        for (did, _), ent in list(entities.items()):
            if did == dev_id and ent.hass is not None:
                ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)

    @callback
    def handle_removed(dev_id: str) -> None:
        for key in [key for key in entities if key[0] == dev_id]:
            entities.pop(key, None)

    hub.register_removed_listener(handle_removed)


class EtCoreBinarySensor(BinarySensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = None  # the device's main feature: named after the device

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, info: dict[str, Any], *,
                 metric: str | None = None, name: str | None = None, icon: str | None = None):
        self._hub = hub
        self._dev_id = dev_id
        kind = dev_class.split(".", 1)[1] if "." in dev_class else ""
        self._attr_device_class = _KINDS.get(kind)
        self._attr_unique_id = f"etcore_{dev_id}_{metric or 'binary'}"
        if metric:
            self._attr_name = name
            self._attr_icon = icon
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

    def set_on(self, on: bool | None) -> None:
        self._attr_is_on = on
        if self.hass is not None:
            self.async_write_ha_state()
