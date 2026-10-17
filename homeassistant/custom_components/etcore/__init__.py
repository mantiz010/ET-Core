from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.storage import Store

from .const import DOMAIN
from .hub import EtCoreHub
from .panel import async_setup_panel, async_unload_panel

PLATFORMS: list[str] = ["switch", "light", "sensor"]


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
    hub = EtCoreHub(hass, entry.data, counter_store=_CmdSeqStore(hass, entry.entry_id))
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
        device = reg.async_get_device(identifiers={(DOMAIN, dev_id)})
        if device and device.name != name and not device.name_by_user:
            reg.async_update_device(device.id, name=name)

    hub.register_listener(_sync_device_name)
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    await async_setup_panel(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    hub: EtCoreHub = hass.data[DOMAIN].pop(entry.entry_id)
    await hub.async_stop()
    if not hass.data[DOMAIN]:
        await async_unload_panel(hass)
    return unload_ok
