from __future__ import annotations

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.storage import Store

from .const import DOMAIN
from .hub import EtCoreHub
from .panel import async_setup_panel, async_unload_panel

PLATFORMS: list[str] = ["switch", "light", "number", "fan", "sensor", "binary_sensor"]


class _CmdSeqStore:
    """Keeps the hub's command seq across Home Assistant restarts."""

    def __init__(self, hass: HomeAssistant, entry_id: str) -> None:
        self._store = Store(hass, 1, f"{DOMAIN}_cmd_seq_{entry_id}")

    async def async_load(self) -> int:
        data = await self._store.async_load()
        return int((data or {}).get("next", 0))

    async def async_save(self, value: int) -> None:
        await self._store.async_save({"next": int(value)})


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    # Options (changed later in the UI) override what was entered at setup.
    hub = EtCoreHub(hass, {**entry.data, **entry.options}, counter_store=_CmdSeqStore(hass, entry.entry_id))
    await hub.async_start()

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = hub

    @callback
    def _sync_device_name(msg: dict) -> None:
        # Entities are created from the first lease, often before the hello; give
        # the device the name from its hello once the hub knows it.
        dev_id = str(msg.get("id") or "")
        name = (hub.devices.get(dev_id) or {}).get("name")
        if not name:
            return
        reg = dr.async_get(hass)
        by_ident = getattr(reg, "async_get_device_by_identifier", None)  # HA 2026.10+
        if by_ident:
            device = by_ident((DOMAIN, dev_id), entry.entry_id)
        else:
            device = reg.async_get_device(identifiers={(DOMAIN, dev_id)})
        if device and device.name != name and not device.name_by_user:
            reg.async_update_device(device.id, name=name)

    hub.register_listener(_sync_device_name)
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    await async_setup_panel(hass)
    entry.async_on_unload(entry.add_update_listener(_async_options_changed))

    if not hass.services.has_service(DOMAIN, "send_command"):
        async def _send_command(call: ServiceCall) -> None:
            dev_id = str(call.data["device_id"])
            for h in hass.data.get(DOMAIN, {}).values():
                if dev_id in h.devices:
                    dev_class = str(h.devices[dev_id].get("class") or "")
                    h.send_command(dev_id, dev_class, dict(call.data["payload"]))
                    return
            raise HomeAssistantError(f"ET-Core device {dev_id} is not known")

        hass.services.async_register(
            DOMAIN, "send_command", _send_command,
            schema=vol.Schema({vol.Required("device_id"): cv.string, vol.Required("payload"): dict}),
        )
    return True


def _dev_id_of(device_entry: dr.DeviceEntry) -> str | None:
    for domain, ident in device_entry.identifiers:
        if domain == DOMAIN:
            return str(ident)
    return None


async def async_remove_config_entry_device(hass: HomeAssistant, entry: ConfigEntry, device_entry: dr.DeviceEntry) -> bool:
    """HA's "Delete device". Only while the device is offline: an online one would
    be back within seconds."""
    hub: EtCoreHub | None = (hass.data.get(DOMAIN) or {}).get(entry.entry_id)
    dev_id = _dev_id_of(device_entry)
    if hub is None or dev_id is None:
        return True
    if (hub.devices.get(dev_id) or {}).get("online"):
        return False
    hub.forget_device(dev_id)
    return True


async def _async_options_changed(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    hub: EtCoreHub = hass.data[DOMAIN].pop(entry.entry_id)
    await hub.async_stop()
    if not hass.data[DOMAIN]:
        await async_unload_panel(hass)
        hass.services.async_remove(DOMAIN, "send_command")
    return unload_ok
