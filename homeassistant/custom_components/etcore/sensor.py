from __future__ import annotations

from typing import Any

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtCoreSensor] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("sensor."):
            return
        # Only state carries readings; lease/ack/hello fields are not sensors.
        if msg.get("type") != "state":
            return

        for key, value in payload.items():
            if key in {"name", "fw", "lib", "features"} or not isinstance(value, (int, float, str, bool)):
                continue
            ent_key = (dev_id, str(key))
            if ent_key not in entities:
                ent = EtCoreSensor(hub, dev_id, str(key))
                entities[ent_key] = ent
                async_add_entities([ent])
            entities[ent_key].set_value(value)

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        dev_id = str(data.get("id") or "")
        for (did, _), ent in list(entities.items()):
            if did == dev_id:
                ent.refresh_availability()
                ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)


class EtCoreSensor(SensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, hub: EtCoreHub, dev_id: str, key: str):
        self._hub = hub
        self._dev_id = dev_id
        self._key = key
        self._attr_name = key.replace("_", " ").capitalize()
        self._attr_unique_id = f"etcore_{dev_id}_{key}"
        self._attr_native_value = None
        self._attr_available = False
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": (hub.devices.get(dev_id) or {}).get("name") or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": "ET-Core Sensor",
        }


    @property
    def available(self) -> bool:
        return bool(self._attr_available)

    def refresh_availability(self) -> None:
        info = self._hub.devices.get(self._dev_id)
        self._attr_available = bool(info and info.get("online", False))

    def set_value(self, value: Any) -> None:
        self._attr_native_value = value
        self.refresh_availability()
        self.async_write_ha_state()
