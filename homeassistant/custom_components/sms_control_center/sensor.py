from __future__ import annotations

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity import DeviceInfo

from . import SIGNAL_UPDATE, SMSControlCenterHub
from .const import DATA_HUB, DOMAIN


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities) -> None:
    hub: SMSControlCenterHub = hass.data[DOMAIN][entry.entry_id][DATA_HUB]
    async_add_entities(
        [
            SMSLastIncomingSensor(hub),
            SMSLastOutgoingSensor(hub),
            SMSInboundCountSensor(hub),
            SMSOutboundCountSensor(hub),
            SMSLastCommandSensor(hub),
            SMSTrustedSenderSensor(hub),
        ]
    )


class SMSBaseSensor(SensorEntity):
    _attr_has_entity_name = True

    def __init__(self, hub: SMSControlCenterHub, key: str, name: str) -> None:
        self.hub = hub
        self._attr_unique_id = f"sms_control_center_{key}"
        self._attr_translation_key = key
        self._attr_name = name

    @property
    def device_info(self) -> DeviceInfo:
        return DeviceInfo(
            identifiers={(DOMAIN, "sms_control_center")},
            name="SMS Control Center",
            manufacturer="ElectronicsTech",
            model="Encrypted ET-Core SMS Gateway" if self.hub.etcore else "MQTT SMS Bridge",
        )

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(
            async_dispatcher_connect(self.hass, SIGNAL_UPDATE, self._handle_update)
        )

    @callback
    def _handle_update(self) -> None:
        self.async_write_ha_state()


class SMSLastIncomingSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "last_incoming", "Last Incoming")

    @property
    def native_value(self):
        return self.hub.last_incoming.message[:255] if self.hub.last_incoming else None

    @property
    def extra_state_attributes(self):
        record = self.hub.last_incoming
        if not record:
            return {}
        return {
            "sender": record.sender,
            "topic": record.topic,
            "received_at": record.received_at,
            "transport": "etcore" if self.hub.etcore else "mqtt",
            "request_id": self.hub.last_request_id,
            "submission_status": self.hub.last_status,
            "trusted": self.hub.is_trusted(record.sender),
        }


class SMSLastOutgoingSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "last_outgoing", "Last Outgoing")

    @property
    def native_value(self):
        return self.hub.last_outgoing.message[:255] if self.hub.last_outgoing else None

    @property
    def extra_state_attributes(self):
        record = self.hub.last_outgoing
        if not record:
            return {}
        return {
            "number": record.number,
            "topic": record.topic,
            "received_at": record.received_at,
            "transport": "etcore" if self.hub.etcore else "mqtt",
            "request_id": self.hub.last_request_id,
            "submission_status": self.hub.last_status,
        }


class SMSInboundCountSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "inbound_count", "Inbound Count")

    @property
    def native_value(self):
        return self.hub.in_count


class SMSOutboundCountSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "outbound_count", "Outbound Count")

    @property
    def native_value(self):
        return self.hub.out_count


class SMSLastCommandSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "last_command", "Last Command")

    @property
    def native_value(self):
        return self.hub.last_command or None


class SMSTrustedSenderSensor(SMSBaseSensor):
    def __init__(self, hub: SMSControlCenterHub) -> None:
        super().__init__(hub, "trusted_sender", "Trusted Sender")

    @property
    def native_value(self):
        return "on" if self.hub.last_sender_trusted else "off"

