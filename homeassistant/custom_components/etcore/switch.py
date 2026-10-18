from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub


def _switch_list(hello: dict[str, Any]) -> list[tuple[str, str]]:
    """[(id, name)] from a hello's "switches": [{"id": "1", "name": "Relay 1"}, ...] (ET-Bus format)."""
    out = []
    for i, sw in enumerate(hello.get("switches") or []):
        if isinstance(sw, dict) and sw.get("id") is not None:
            sid = str(sw["id"])
            out.append((sid, str(sw.get("name") or f"Switch {sid}")))
        elif isinstance(sw, str):
            out.append((sw, f"Switch {sw}"))
        else:
            out.append((str(i + 1), f"Switch {i + 1}"))
    return out


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtCoreSwitch] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("switch.") or msg.get("type") not in ("hello", "lease", "state"):
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return  # wait for the hello: it says whether this is one switch or several

        switches = _switch_list(info.get("hello") or {})
        keys = [(dev_id, sid) for sid, _ in switches] or [(dev_id, "main")]
        new = []
        for (sid, sname) in switches or [("main", None)]:
            if (dev_id, sid) not in entities:
                ent = EtCoreSwitch(hub, dev_id, dev_class, info, sid, sname)
                entities[(dev_id, sid)] = ent
                new.append(ent)
        if new:
            async_add_entities(new)

        reported = payload.get("switches") if msg.get("type") == "state" else None
        for key in keys:
            ent = entities[key]
            ent.refresh_availability()
            if isinstance(reported, dict):
                if key[1] in reported:
                    ent._attr_is_on = bool(reported[key[1]])
            elif msg.get("type") == "state" and "on" in payload and key[1] == "main":
                ent._attr_is_on = bool(payload["on"])
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

    @callback
    def handle_removed(dev_id: str) -> None:
        # Device removed in HA: forget our entities so they are created again if it returns.
        for key in [k for k in entities if (k[0] if isinstance(k, tuple) else k) == dev_id]:
            entities.pop(key)

    hub.register_removed_listener(handle_removed)


class EtCoreSwitch(SwitchEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, hub: EtCoreHub, dev_id: str, dev_class: str, info: dict[str, Any],
                 switch_id: str, switch_name: str | None):
        self._hub = hub
        self._dev_id = dev_id
        self._dev_class = dev_class
        self._switch_id = switch_id
        if switch_id == "main":
            # One switch: the entity is the device (named by __init__.py from the hello).
            self._attr_name = None
            self._attr_unique_id = f"etcore_{dev_id}_switch"
        else:
            self._attr_name = switch_name
            self._attr_unique_id = f"etcore_{dev_id}_switch_{switch_id}"
        self._attr_is_on = False
        self._attr_available = False
        hello = info.get("hello") or {}
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": info.get("name") or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": str(hello.get("model") or ("ET-Core Switch" if switch_id == "main" else "ET-Core Multi-Switch")),
            "sw_version": str(info.get("fw") or ""),
        }

    @property
    def is_on(self) -> bool:
        return bool(self._attr_is_on)

    @property
    def available(self) -> bool:
        return bool(self._attr_available)

    def refresh_availability(self) -> None:
        info = self._hub.devices.get(self._dev_id)
        self._attr_available = bool(info and info.get("online", False))

    def _send(self, on: bool) -> None:
        payload: dict[str, Any] = {"on": on}
        if self._switch_id != "main":
            payload["switch_id"] = self._switch_id
        self._hub.send_command(self._dev_id, self._dev_class, payload)

    async def async_turn_on(self, **kwargs: Any) -> None:
        self._send(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        self._send(False)
