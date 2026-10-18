from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any

import voluptuous as vol
from aiohttp import web
from homeassistant.components import websocket_api
from homeassistant.components.frontend import async_register_built_in_panel, async_remove_panel
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr, entity_registry as er

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

PANEL_URL_PATH = "et-core"
VIEW_URL = f"/{DOMAIN}/etcore.html"
DIAG_URL = f"/{DOMAIN}/api/diag"
PANEL_TITLE = "ET-Core"
PANEL_ICON = "mdi:lan-connect"

_HTML_CACHE: str | None = None


def _dumps(data) -> str:
    return json.dumps(data, default=str)


class EtCoreHtmlView(HomeAssistantView):
    # The sidebar loads this page in an iframe, which cannot send the auth header.
    # It is a fixed page with no data in it; it reads the logged-in user's token
    # from the HA frontend and calls the diagnostics API, which needs auth.
    url = VIEW_URL
    name = "etcore:html"
    requires_auth = False

    async def get(self, request: web.Request) -> web.Response:
        global _HTML_CACHE
        if _HTML_CACHE is None:
            return web.Response(text="ET-Core UI not loaded", status=500, content_type="text/plain")
        return web.Response(text=_HTML_CACHE, status=200, content_type="text/html")


class EtCoreDiagApiView(HomeAssistantView):
    url = DIAG_URL
    name = "etcore:diag"
    requires_auth = True

    async def get(self, request: web.Request) -> web.Response:
        try:
            since = int(request.query["since"]) if "since" in request.query else None
        except ValueError:
            since = None
        return web.json_response(_diag(request.app["hass"], since), dumps=_dumps)


def _diag(hass: HomeAssistant, since: int | None) -> dict[str, Any]:
    dev_reg = dr.async_get(hass)
    ent_reg = er.async_get(hass)
    hubs = []
    for hub in (hass.data.get(DOMAIN) or {}).values():
        get_snapshot = getattr(hub, "get_diag_snapshot", None)
        if not callable(get_snapshot):
            continue
        snap = get_snapshot(since)
        # Which HA entities belong to each device, so the page can show and
        # control them through the normal HA services.
        for dev in snap["devices"]:
            device = dev_reg.async_get_device(identifiers={(DOMAIN, str(dev.get("id")))})
            dev["ha_device_id"] = device.id if device else None
            dev["entities"] = [
                {"entity_id": e.entity_id, "name": e.name or e.original_name, "disabled": bool(e.disabled_by)}
                for e in (er.async_entries_for_device(ent_reg, device.id) if device else [])
            ]
        hubs.append(snap)
    return json.loads(_dumps({"ok": True, "domain": DOMAIN, "hubs": hubs}))


# The sidebar page asks over HA's own websocket (the frontend's connection, which
# renews its login itself). Polling the HTTP API with a copied token got the PC
# IP-banned once the token expired: every 401 counts as a failed login.
@websocket_api.websocket_command({vol.Required("type"): "etcore/diag", vol.Optional("since"): int})
@callback
def _ws_diag(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    connection.send_result(msg["id"], _diag(hass, msg.get("since")))


async def async_setup_panel(hass: HomeAssistant) -> None:
    global _HTML_CACHE

    if _HTML_CACHE is None:
        file_path = Path(__file__).parent / "www" / "etcore.html"

        def _read_file() -> str:
            return file_path.read_text(encoding="utf-8")

        try:
            _HTML_CACHE = await hass.async_add_executor_job(_read_file)
        except Exception as exc:
            _LOGGER.error("ET-Core UI file read failed: %s", exc)
            _HTML_CACHE = "<!doctype html><html><body><h2>ET-Core UI missing</h2></body></html>"

    if not hass.data.get(f"{DOMAIN}_panel_loaded"):
        hass.http.register_view(EtCoreHtmlView)
        hass.http.register_view(EtCoreDiagApiView)
        websocket_api.async_register_command(hass, _ws_diag)
        hass.data[f"{DOMAIN}_panel_loaded"] = True

    async_register_built_in_panel(
        hass,
        component_name="iframe",
        sidebar_title=PANEL_TITLE,
        sidebar_icon=PANEL_ICON,
        frontend_url_path=PANEL_URL_PATH,
        config={"url": VIEW_URL},
        require_admin=False,
    )


async def async_unload_panel(hass: HomeAssistant) -> None:
    try:
        async_remove_panel(hass, PANEL_URL_PATH)
    except Exception:
        pass
