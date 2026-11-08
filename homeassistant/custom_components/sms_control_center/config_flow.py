from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant import config_entries

from .const import (
    CONF_INCOMING_TOPIC,
    CONF_MAX_LOG,
    CONF_OUTGOING_TOPIC,
    CONF_TRUSTED_NUMBERS,
    DEFAULT_INCOMING_TOPIC,
    DEFAULT_MAX_LOG,
    DEFAULT_OUTGOING_TOPIC,
    DOMAIN,
)


class SMSControlCenterConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    @staticmethod
    def async_get_options_flow(config_entry):
        return SMSOptionsFlow()

    async def async_step_import(self, user_input: dict[str, Any]):
        data = {
            CONF_INCOMING_TOPIC: user_input.get(CONF_INCOMING_TOPIC, DEFAULT_INCOMING_TOPIC),
            CONF_OUTGOING_TOPIC: user_input.get(CONF_OUTGOING_TOPIC, DEFAULT_OUTGOING_TOPIC),
            CONF_TRUSTED_NUMBERS: user_input.get(CONF_TRUSTED_NUMBERS, ""),
            CONF_MAX_LOG: user_input.get(CONF_MAX_LOG, DEFAULT_MAX_LOG),
        }
        await self.async_set_unique_id(DOMAIN)
        self._abort_if_unique_id_configured()
        return self.async_create_entry(title="SMS Control Center", data=data)

    async def async_step_user(self, user_input: dict[str, Any] | None = None):
        if user_input is not None:
            await self.async_set_unique_id(DOMAIN)
            self._abort_if_unique_id_configured()
            return self.async_create_entry(title="SMS Control Center", data=user_input)

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required("transport", default="etcore"): vol.In(["etcore", "mqtt"]),
                    vol.Required(CONF_INCOMING_TOPIC, default=DEFAULT_INCOMING_TOPIC): str,
                    vol.Required(CONF_OUTGOING_TOPIC, default=DEFAULT_OUTGOING_TOPIC): str,
                    vol.Optional(CONF_TRUSTED_NUMBERS, default=""): str,
                    vol.Optional(CONF_MAX_LOG, default=DEFAULT_MAX_LOG): int,
                }
            ),
            errors={},
        )


class SMSOptionsFlow(config_entries.OptionsFlow):
    async def async_step_init(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title="", data=user_input)
        return self.async_show_form(step_id="init", data_schema=vol.Schema({
            vol.Required("transport", default=self.config_entry.options.get("transport", "mqtt")):vol.In(["mqtt", "etcore"])
        }))
