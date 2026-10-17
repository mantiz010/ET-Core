from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtCoreSwitch] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("switch."):
            return

        if msg.get("type") in ("hello", "lease", "state"):
            key = (dev_id, "main")
            if key not in entities:
                ent = EtCoreSwitch(hub, dev_id, dev_class, hub.devices.get(dev_id, {}).get("name") or payload.get("name", dev_id))
                entities[key] = ent
                async_add_entities([ent])
            ent = entities[key]
            ent.refresh_availability()
            if msg.get("type") == "state" and "on" in payload:
                ent.set_state(bool(payload["on"]))
            else:
                ent.async_write_ha_state()

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        dev_id = data.get("id")
        for (did, _), ent in list(entities.items()):
            if did == dev_id:
                ent.refresh_availability()
                ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)


class EtCoreSwitch(SwitchEntity):
    _attr_should_poll = False
    # The device's main feature: the entity takes the device name, which
    # __init__.py keeps in step with the device's hello.
    _attr_has_entity_name = True
    _attr_name = None

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, name: str):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._attr_unique_id = f"etcore_{dev_id}_switch"
        self._attr_is_on = False
        self._attr_available = False
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            # The hello's name when known (the entity is usually created from the
            # hello); __init__.py renames the device if the hello comes later.
            "name": name or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": "ET-Core Switch",
        }


    @property
    def is_on(self) -> bool:
        return self._attr_is_on

    @property
    def available(self) -> bool:
        return bool(self._attr_available)

    def refresh_availability(self) -> None:
        info = self._hub.devices.get(self._dev_id)
        self._attr_available = bool(info and info.get("online", False))

    def set_state(self, on: bool) -> None:
        self._attr_is_on = on
        self.refresh_availability()
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        self._hub.send_command(self._dev_id, self._dev_class, {"on": True})

    async def async_turn_off(self, **kwargs: Any) -> None:
        self._hub.send_command(self._dev_id, self._dev_class, {"on": False})
