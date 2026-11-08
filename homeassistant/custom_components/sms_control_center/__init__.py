from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
import json
import logging
from typing import Any

import voluptuous as vol

from homeassistant.components import mqtt
from .etcore_transport import EtCoreSmsTransport
from homeassistant import config_entries
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.dispatcher import async_dispatcher_send

from .const import (
    CONF_INCOMING_TOPIC,
    CONF_MAX_LOG,
    CONF_OUTGOING_TOPIC,
    CONF_TRUSTED_NUMBERS,
    DATA_HUB,
    DEFAULT_MAX_LOG,
    DOMAIN,
)

_LOGGER = logging.getLogger(__name__)

PLATFORMS = ["sensor", "button"]
SIGNAL_UPDATE = f"{DOMAIN}_update"
CONF_MESSAGE = "message"

CONFIG_SCHEMA = vol.Schema(
    {
        DOMAIN: vol.Schema(
            {
                vol.Optional(CONF_INCOMING_TOPIC, default="home/sms/incoming"): cv.string,
                vol.Optional(CONF_OUTGOING_TOPIC, default="home/sms/send"): cv.string,
                vol.Optional(CONF_TRUSTED_NUMBERS, default=""): cv.string,
                vol.Optional(CONF_MAX_LOG, default=DEFAULT_MAX_LOG): cv.positive_int,
            }
        )
    },
    extra=vol.ALLOW_EXTRA,
)


@dataclass
class SMSRecord:
    direction: str
    topic: str
    message: str
    number: str = ""
    sender: str = ""
    received_at: str = field(default_factory=lambda: datetime.now().isoformat(timespec="seconds"))


class SMSControlCenterHub:
    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.incoming_topic = entry.data[CONF_INCOMING_TOPIC]
        self.outgoing_topic = entry.data[CONF_OUTGOING_TOPIC]
        self.max_log = int(entry.data.get(CONF_MAX_LOG, DEFAULT_MAX_LOG))
        trusted = entry.data.get(CONF_TRUSTED_NUMBERS, "")
        self.trusted_numbers = [part.strip() for part in trusted.split(",") if part.strip()]
        self.log: list[SMSRecord] = []
        self.in_count = 0
        self.out_count = 0
        self.last_incoming: SMSRecord | None = None
        self.last_outgoing: SMSRecord | None = None
        self.last_command = ""
        self.last_sender_trusted = False
        self._unsubs: list[Any] = []
        self.etcore = None
        self.modem_ready = False
        self.last_request_id = None
        self.last_status = None

    async def async_start(self) -> None:
        if self.entry.options.get("transport", self.entry.data.get("transport")) == "etcore":
            self.etcore = EtCoreSmsTransport(self)
            await self.etcore.start()
            return
        self._unsubs.append(
            await mqtt.async_subscribe(self.hass, self.incoming_topic, self._handle_incoming, 0)
        )
        self._unsubs.append(
            await mqtt.async_subscribe(self.hass, self.outgoing_topic, self._handle_outgoing, 0)
        )

    async def async_stop(self) -> None:
        if self.etcore:
            await self.etcore.stop()
        for unsub in self._unsubs:
            unsub()
        self._unsubs.clear()

    @callback
    def _handle_incoming(self, msg) -> None:
        data = _parse_payload(msg.payload)
        sender = str(data.get("sender") or data.get("number") or "")
        message = str(data.get("message") or msg.payload or "")
        record = SMSRecord("in", msg.topic, message, sender=sender)
        self.in_count += 1
        self.last_incoming = record
        self.last_command = message.strip().lower()
        self.last_sender_trusted = self.is_trusted(sender)
        self._append(record)

    @callback
    def _handle_outgoing(self, msg) -> None:
        data = _parse_payload(msg.payload)
        number = str(data.get("number") or "")
        message = str(data.get("message") or msg.payload or "")
        record = SMSRecord("out", msg.topic, message, number=number)
        self.out_count += 1
        self.last_outgoing = record
        self._append(record)

    def _append(self, record: SMSRecord) -> None:
        self.log.append(record)
        self.log = self.log[-self.max_log :]
        async_dispatcher_send(self.hass, SIGNAL_UPDATE)

    def is_trusted(self, number: str) -> bool:
        if not self.trusted_numbers:
            return True
        normalized = _normalize_number(number)
        return any(_normalize_number(item) == normalized for item in self.trusted_numbers)

    async def async_send_sms(self, message: str, number: str | None = None, topic: str | None = None) -> None:
        if self.etcore:
            await self.etcore.send(message, number, topic)
            return
        payload: dict[str, str] = {"message": message}
        if number:
            payload["number"] = number
        await mqtt.async_publish(
            self.hass,
            topic or self.outgoing_topic,
            json.dumps(payload, separators=(",", ":")),
            qos=0,
            retain=False,
        )

    def record_outgoing(self, message, number, topic, request_id):
        record = SMSRecord("out", topic, message, number=number)
        self.out_count += 1
        self.last_outgoing = record
        self.last_request_id = request_id
        self.last_status = "submitted_to_gateway"
        self._append(record)

    def etcore_event(self, event):
        if event["kind"] == "incoming":
            self._handle_incoming(type("Message", (), {"payload":json.dumps(event), "topic":"etcore/sms/incoming"})())
        elif event.get("request_id") == self.last_request_id:
            # A queued event may arrive after the final result across UDP.
            if not (self.last_status in ("modem_accepted","submission_unknown","not_submitted") and event["status"]=="queued"):
                self.last_status = event["status"]
            async_dispatcher_send(self.hass, SIGNAL_UPDATE)

    def clear_log(self) -> None:
        self.log.clear()
        async_dispatcher_send(self.hass, SIGNAL_UPDATE)


async def async_setup(hass: HomeAssistant, config: dict[str, Any]) -> bool:
    if DOMAIN in config:
        hass.async_create_task(
            hass.config_entries.flow.async_init(
                DOMAIN,
                context={"source": config_entries.SOURCE_IMPORT},
                data=config[DOMAIN],
            )
        )
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    hub = SMSControlCenterHub(hass, entry)
    await hub.async_start()
    entry.async_on_unload(entry.add_update_listener(_options_changed))

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = {DATA_HUB: hub}

    async def send_sms(call: ServiceCall) -> None:
        await hub.async_send_sms(
            message=call.data[CONF_MESSAGE],
            number=call.data.get("number"),
            topic=call.data.get("topic"),
        )

    async def clear_log(call: ServiceCall) -> None:
        hub.clear_log()

    hass.services.async_register(
        DOMAIN,
        "send_sms",
        send_sms,
        schema=vol.Schema(
            {
                vol.Required(CONF_MESSAGE): cv.string,
                vol.Optional("number"): cv.string,
                vol.Optional("topic"): cv.string,
            }
        ),
    )
    hass.services.async_register(DOMAIN, "clear_log", clear_log)

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    data = hass.data[DOMAIN].pop(entry.entry_id)
    await data[DATA_HUB].async_stop()
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


def _parse_payload(payload: Any) -> dict[str, Any]:
    if isinstance(payload, bytes):
        payload = payload.decode("utf-8", errors="replace")
    if not isinstance(payload, str):
        return {}
    try:
        parsed = json.loads(payload)
    except (TypeError, ValueError):
        return {}
    return parsed if isinstance(parsed, dict) else {}


def _normalize_number(number: str) -> str:
    return "".join(ch for ch in str(number) if ch.isdigit())


async def _options_changed(hass, entry):
    await hass.config_entries.async_reload(entry.entry_id)
