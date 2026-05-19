from __future__ import annotations

from homeassistant import config_entries

from .const import DOMAIN


class EtCoreConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        return self.async_create_entry(title="ET-Core", data={})
