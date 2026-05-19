from __future__ import annotations

import voluptuous as vol

from homeassistant import config_entries

from .const import CONF_PSK_HEX, DOMAIN


class EtCoreConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if user_input is not None:
            psk = str(user_input.get(CONF_PSK_HEX) or "").strip()
            data = {CONF_PSK_HEX: psk} if psk else {}
            return self.async_create_entry(title="ET-Core", data=data)

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Optional(CONF_PSK_HEX): str}),
        )
