from __future__ import annotations

import re

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback

from .const import CONF_PORT, CONF_PSK_HEX, DOMAIN, PORT


def _clean_psk(value: str | None) -> str:
    return re.sub(r"[^0-9a-fA-F]", "", str(value or "")).lower()


def _schema(psk: str, port: int) -> vol.Schema:
    return vol.Schema({
        vol.Optional(CONF_PSK_HEX, default=psk): str,
        vol.Required(CONF_PORT, default=port): vol.All(vol.Coerce(int), vol.Range(min=1024, max=65535)),
    })


def _validate(user_input: dict) -> tuple[dict, dict[str, str]]:
    psk = _clean_psk(user_input.get(CONF_PSK_HEX))
    if psk and len(psk) != 64:
        return {}, {CONF_PSK_HEX: "invalid_psk"}
    return {CONF_PSK_HEX: psk, CONF_PORT: int(user_input.get(CONF_PORT) or PORT)}, {}


class EtCoreConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors: dict[str, str] = {}
        if user_input is not None:
            data, errors = _validate(user_input)
            if not errors:
                return self.async_create_entry(title="ET-Core", data=data)
        return self.async_show_form(step_id="user", data_schema=_schema("", PORT), errors=errors)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return EtCoreOptionsFlow()


class EtCoreOptionsFlow(config_entries.OptionsFlow):
    """Change the key or port without removing the integration (it reloads)."""

    async def async_step_init(self, user_input=None):
        current = {**self.config_entry.data, **self.config_entry.options}
        errors: dict[str, str] = {}
        if user_input is not None:
            data, errors = _validate(user_input)
            if not errors:
                return self.async_create_entry(title="", data=data)
        return self.async_show_form(
            step_id="init",
            data_schema=_schema(str(current.get(CONF_PSK_HEX) or ""), int(current.get(CONF_PORT) or PORT)),
            errors=errors,
        )
