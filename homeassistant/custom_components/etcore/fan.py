from __future__ import annotations

from typing import Any

from homeassistant.components.fan import FanEntity, FanEntityFeature
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub

DEFAULT_PRESETS = ["off", "low", "medium", "high"]  # same as ET-Bus


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    """ET-Bus fan classes: fan.speed ({"on", "speed": 0-100}) and fan.preset ({"on", "preset"})."""
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtCoreFan] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or dev_class not in ("fan.speed", "fan.preset") or msg.get("type") not in ("hello", "lease", "state"):
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return  # wait for the hello (name, presets)
        if dev_id not in entities:
            entities[dev_id] = EtCoreFan(hub, dev_id, dev_class, info)
            async_add_entities([entities[dev_id]])
        ent = entities[dev_id]
        if msg.get("type") == "state":
            ent.set_state(payload)
        else:
            ent.async_write_ha_state()

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        ent = entities.get(str(data.get("id") or ""))
        if ent:
            ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)


class EtCoreFan(FanEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = None

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, info: dict[str, Any]):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._attr_unique_id = f"etcore_{dev_id}_fan"
        self._is_on = False
        self._percentage = 0
        self._preset: str | None = None
        hello = info.get("hello") or {}
        presets = hello.get("presets")
        self._presets = [str(p) for p in presets] if isinstance(presets, list) and presets else list(DEFAULT_PRESETS)
        # TURN_ON/TURN_OFF must be declared or HA 2026.10 rejects fan.turn_on/off.
        base = FanEntityFeature.TURN_ON | FanEntityFeature.TURN_OFF
        if dev_class == "fan.speed":
            self._attr_supported_features = base | FanEntityFeature.SET_SPEED
        else:
            self._attr_supported_features = base | FanEntityFeature.PRESET_MODE
            self._attr_preset_modes = self._presets
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": info.get("name") or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": str(hello.get("model") or "ET-Core Fan"),
            "sw_version": str(info.get("fw") or ""),
        }

    @property
    def available(self) -> bool:
        return bool((self._hub.devices.get(self._dev_id) or {}).get("online", False))

    @property
    def is_on(self) -> bool:
        return self._is_on

    @property
    def percentage(self) -> int | None:
        return self._percentage if self._dev_class == "fan.speed" else None

    @property
    def preset_mode(self) -> str | None:
        return self._preset if self._dev_class == "fan.preset" else None

    def set_state(self, payload: dict[str, Any]) -> None:
        """The device is the source of truth: HA shows what it reports."""
        if "on" in payload:
            self._is_on = bool(payload["on"])
        if self._dev_class == "fan.speed" and "speed" in payload:
            self._percentage = max(0, min(100, int(payload["speed"])))
        if self._dev_class == "fan.preset" and "preset" in payload:
            self._preset = str(payload["preset"])
        self.async_write_ha_state()

    def _send(self, on: bool, percentage: int | None = None, preset: str | None = None) -> None:
        # Full state like ET-Bus sends, so ET-Bus fan sketches port unchanged.
        payload: dict[str, Any] = {"on": on}
        if self._dev_class == "fan.speed":
            payload["speed"] = int(self._percentage if percentage is None else percentage)
        else:
            payload["preset"] = preset or self._preset or ("low" if on else "off")
        self._hub.send_command(self._dev_id, self._dev_class, payload)

    async def async_turn_on(self, percentage: int | None = None, preset_mode: str | None = None, **kwargs: Any) -> None:
        if self._dev_class == "fan.speed":
            pct = percentage if percentage is not None else (self._percentage or 100)
            self._send(pct > 0, pct)
        else:
            preset = preset_mode or (self._preset if self._preset not in (None, "off") else None)
            preset = preset or next((p for p in self._presets if p != "off"), "low")
            self._send(True, preset=preset)

    async def async_turn_off(self, **kwargs: Any) -> None:
        if self._dev_class == "fan.speed":
            self._send(False, 0)
        else:
            self._send(False, preset="off")

    async def async_set_percentage(self, percentage: int) -> None:
        self._send(percentage > 0, percentage)

    async def async_set_preset_mode(self, preset_mode: str) -> None:
        self._send(preset_mode != "off", preset=preset_mode)
