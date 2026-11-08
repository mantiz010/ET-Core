from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import DeviceInfo

from . import SMSControlCenterHub
from .const import DATA_HUB, DOMAIN


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities) -> None:
    hub: SMSControlCenterHub = hass.data[DOMAIN][entry.entry_id][DATA_HUB]
    async_add_entities([SMSTestButton(hub), SMSClearLogButton(hub)])


class SMSBaseButton(ButtonEntity):
    _attr_has_entity_name = True

    def __init__(self, hub: SMSControlCenterHub, key: str, name: str) -> None:
        self.hub = hub
        self._attr_unique_id = f"sms_control_center_{key}"
        self._attr_name = name

    @property
    def device_info(self) -> DeviceInfo:
        return DeviceInfo(
            identifiers={(DOMAIN, "sms_control_center")},
            name="SMS Control Center",
            manufacturer="ElectronicsTech",
            model="MQTT SMS Bridge",
        )


class SMSTestButton(SMSBaseButton):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "send_test", "Send Test SMS")

    async def async_press(self) -> None:
        await self.hub.async_send_sms("SMS Control Center test")


class SMSClearLogButton(SMSBaseButton):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "clear_log", "Clear SMS Log")

    async def async_press(self) -> None:
        self.hub.clear_log()

