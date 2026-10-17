from __future__ import annotations

from typing import Any

from homeassistant.components.light import (
    ATTR_BRIGHTNESS,
    ATTR_EFFECT,
    ATTR_RGB_COLOR,
    ColorMode,
    LightEntity,
    LightEntityFeature,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtCoreLight] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("light."):
            return

        if msg.get("type") in ("hello", "lease", "state"):
            if dev_id not in entities:
                ent = EtCoreLight(hub, dev_id, dev_class, hub.devices.get(dev_id, {}).get("name") or payload.get("name", dev_id))
                entities[dev_id] = ent
                async_add_entities([ent])
            ent = entities[dev_id]
            ent.refresh_availability()
            if msg.get("type") == "state":
                ent.set_state(payload)
            else:
                ent.async_write_ha_state()

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        ent = entities.get(str(data.get("id") or ""))
        if ent:
            ent.refresh_availability()
            ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)


class EtCoreLight(LightEntity):
    _attr_should_poll = False
    # The device's main feature: the entity takes the device name, which
    # __init__.py keeps in step with the device's hello.
    _attr_has_entity_name = True
    _attr_name = None
    _attr_supported_color_modes = {ColorMode.RGB}
    _attr_color_mode = ColorMode.RGB

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, name: str):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._attr_unique_id = f"etcore_{dev_id}_light"
        self._attr_is_on = False
        self._attr_brightness = 255
        self._attr_rgb_color = (255, 255, 255)
        self._attr_effect = None
        self._attr_available = False
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            # The hello's name when known (the entity is usually created from the
            # hello); __init__.py renames the device if the hello comes later.
            "name": name or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": "ET-Core Light",
        }


    @property
    def is_on(self) -> bool:
        return bool(self._attr_is_on)

    @property
    def effect_list(self) -> list[str] | None:
        # From the device's hello ("effects": [...]), so a new firmware can add some.
        return list(self._hub.devices.get(self._dev_id, {}).get("effects") or []) or None

    @property
    def supported_features(self) -> LightEntityFeature:
        return LightEntityFeature.EFFECT if self.effect_list else LightEntityFeature(0)

    @property
    def available(self) -> bool:
        return bool(self._attr_available)

    def refresh_availability(self) -> None:
        info = self._hub.devices.get(self._dev_id)
        self._attr_available = bool(info and info.get("online", False))

    def set_state(self, payload: dict[str, Any]) -> None:
        if "on" in payload:
            self._attr_is_on = bool(payload["on"])
        if "brightness" in payload:
            self._attr_brightness = max(0, min(255, int(payload["brightness"])))
        if all(k in payload for k in ("r", "g", "b")):
            self._attr_rgb_color = (
                max(0, min(255, int(payload["r"]))),
                max(0, min(255, int(payload["g"]))),
                max(0, min(255, int(payload["b"]))),
            )
        if "effect" in payload:
            self._attr_effect = str(payload["effect"]) if payload["effect"] else None
        self.refresh_availability()
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        payload: dict[str, Any] = {"on": True}
        if ATTR_BRIGHTNESS in kwargs:
            payload["brightness"] = int(kwargs[ATTR_BRIGHTNESS])
        if ATTR_RGB_COLOR in kwargs:
            r, g, b = kwargs[ATTR_RGB_COLOR]
            payload.update({"r": int(r), "g": int(g), "b": int(b)})
        if ATTR_EFFECT in kwargs:
            payload["effect"] = str(kwargs[ATTR_EFFECT])
        self._hub.send_command(self._dev_id, self._dev_class, payload)

    async def async_turn_off(self, **kwargs: Any) -> None:
        self._hub.send_command(self._dev_id, self._dev_class, {"on": False})
