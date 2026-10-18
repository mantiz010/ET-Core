from __future__ import annotations

from typing import Any

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    """Effect speed for lights that report one (ET-Bus "speed", 1-255).

    Home Assistant's light service has no speed field, so it gets its own control.
    """
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtCoreEffectSpeed] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("light.") or msg.get("type") != "state" or "speed" not in payload:
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return
        if dev_id not in entities:
            entities[dev_id] = EtCoreEffectSpeed(hub, dev_id, dev_class, info)
            async_add_entities([entities[dev_id]])
        entities[dev_id].update_from_device(payload["speed"])

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        ent = entities.get(str(data.get("id") or ""))
        if ent:
            ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)

    @callback
    def handle_removed(dev_id: str) -> None:
        # Device removed in HA: forget our entities so they are created again if it returns.
        for key in [k for k in entities if (k[0] if isinstance(k, tuple) else k) == dev_id]:
            entities.pop(key)

    hub.register_removed_listener(handle_removed)


class EtCoreEffectSpeed(NumberEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = "Effect speed"
    _attr_icon = "mdi:speedometer"
    _attr_native_min_value = 1
    _attr_native_max_value = 255
    _attr_native_step = 1
    _attr_mode = NumberMode.SLIDER

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, info: dict[str, Any]):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._attr_unique_id = f"etcore_{dev_id}_effect_speed"
        self._attr_native_value = None
        self._attr_device_info = {"identifiers": {(DOMAIN, dev_id)}, "name": info.get("name") or dev_id}

    @property
    def available(self) -> bool:
        return bool((self._hub.devices.get(self._dev_id) or {}).get("online", False))

    def update_from_device(self, value: Any) -> None:
        self._attr_native_value = max(1, min(255, int(value)))
        self.async_write_ha_state()

    async def async_set_native_value(self, value: float) -> None:
        # Shown once the device confirms it in its state.
        self._hub.send_command(self._dev_id, self._dev_class, {"speed": int(value)})
