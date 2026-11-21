from __future__ import annotations

import json
import asyncio
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

from .const import DOMAIN, MULTICAST_GROUP
from .health_profiles import PROFILES, describe_device
from homeassistant.helpers.storage import Store

_LOGGER = logging.getLogger(__name__)

PANEL_URL_PATH = "et-core"
VIEW_URL = f"/{DOMAIN}/etcore.html"
DIAG_URL = f"/{DOMAIN}/api/diag"
PANEL_TITLE = "ET-Core"
PANEL_ICON = "mdi:lan-connect"
HTML_FILE = Path(__file__).parent / "www" / "etcore.html"

_html: tuple[float, str] | None = None  # (mtime, text)


def _dumps(data) -> str:
    return json.dumps(data, default=str)


def _read_html() -> str:
    """The page, re-read when the file changes (no HA restart to update the panel)."""
    global _html
    try:
        mtime = HTML_FILE.stat().st_mtime
        if _html is None or _html[0] != mtime:
            _html = (mtime, HTML_FILE.read_text(encoding="utf-8"))
        return _html[1]
    except OSError as exc:
        _LOGGER.error("ET-Core UI file read failed: %s", exc)
        return "<!doctype html><html><body><h2>ET-Core UI missing</h2></body></html>"


class EtCoreHtmlView(HomeAssistantView):
    # The sidebar loads this page in an iframe, which cannot send the auth header.
    # It is a fixed page with no data in it; everything it shows comes over the
    # logged-in user's own HA websocket.
    url = VIEW_URL
    name = "etcore:html"
    requires_auth = False

    async def get(self, request: web.Request) -> web.Response:
        hass: HomeAssistant = request.app["hass"]
        text = await hass.async_add_executor_job(_read_html)
        return web.Response(text=text, status=200, content_type="text/html",
                            headers={"Cache-Control": "no-cache"})


class EtCoreDiagApiView(HomeAssistantView):
    """For tools and scripts (needs a token). The panel uses the websocket."""
    url = DIAG_URL
    name = "etcore:diag"
    requires_auth = True

    async def get(self, request: web.Request) -> web.Response:
        try:
            since = int(request.query["since"]) if "since" in request.query else None
        except ValueError:
            since = None
        return web.json_response(_diag(request.app["hass"], since), dumps=_dumps)


def _hubs(hass: HomeAssistant) -> dict[str, Any]:
    return dict(hass.data.get(DOMAIN) or {})


def _ha_device(hass: HomeAssistant, dev_id: str, entry_id: str | None = None) -> dr.DeviceEntry | None:
    reg = dr.async_get(hass)
    ident = (DOMAIN, str(dev_id))
    by_ident = getattr(reg, "async_get_device_by_identifier", None)  # HA 2026.10+
    if by_ident is None:
        return reg.async_get_device(identifiers={ident})
    for eid in [entry_id] if entry_id else list(_hubs(hass)):
        device = by_ident(ident, eid)
        if device is not None:
            return device
    return None


def _profile_state(hass: HomeAssistant) -> dict:
    key = f"{DOMAIN}_health_profiles"
    if key not in hass.data:
        hass.data[key] = {"data": {}, "ready": False, "lock": asyncio.Lock(),
                          "store": Store(hass, 1, key)}
    return hass.data[key]


async def _load_profiles(hass: HomeAssistant) -> None:
    state = _profile_state(hass)
    async with state["lock"]:
        if state["ready"]:
            return
        saved = await state["store"].async_load() or {}
        state["data"] = {
            entry: {dev: profile for dev, profile in devices.items()
                    if isinstance(dev, str) and isinstance(profile, str) and profile in PROFILES}
            for entry, devices in saved.items()
            if isinstance(entry, str) and isinstance(devices, dict)
        } if isinstance(saved, dict) else {}
        state["ready"] = True


def _diag(hass: HomeAssistant, since: int | None) -> dict[str, Any]:
    ent_reg = er.async_get(hass)
    hubs = []
    for entry_id, hub in _hubs(hass).items():
        get_snapshot = getattr(hub, "get_diag_snapshot", None)
        if not callable(get_snapshot):
            continue
        snap = get_snapshot(since)
        snap["entry_id"] = entry_id
        entry = hass.config_entries.async_get_entry(entry_id)
        snap["title"] = entry.title if entry else "ET-Core"
        # Which HA entities belong to each device, so the page can show and
        # control them through the normal HA services.
        for dev in snap["devices"]:
            override = _profile_state(hass)["data"].get(entry_id, {}).get(dev["id"], "auto")
            dev["health_profile"] = describe_device(dev, override)
            device = _ha_device(hass, dev.get("id"), entry_id)
            dev["ha_device_id"] = device.id if device else None
            dev["ha_name"] = device.name_by_user if device else None
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


@websocket_api.websocket_command({
    vol.Required("type"): "etcore/rename",
    vol.Required("dev_id"): str,
    vol.Required("name"): vol.All(str, vol.Length(max=64)),
})
@websocket_api.require_admin
@callback
def _ws_rename(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    """Rename in HA's device registry (name_by_user), like HA's own rename. Empty
    name = back to the name the device reports."""
    device = _ha_device(hass, msg["dev_id"])
    if device is None:
        connection.send_error(msg["id"], "not_found", "Home Assistant has no device for this id yet")
        return
    name = msg["name"].strip() or None
    dr.async_get(hass).async_update_device(device.id, name_by_user=name)
    connection.send_result(msg["id"], {"name": name})


@websocket_api.websocket_command({vol.Required("type"): "etcore/remove", vol.Required("dev_id"): str})
@websocket_api.require_admin
@callback
def _ws_remove(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    """Remove a device that is gone for good (offline only: an online one would be
    back with its next packet)."""
    dev_id = msg["dev_id"]
    for entry_id, hub in _hubs(hass).items():
        info = hub.devices.get(dev_id)
        if info is None:
            continue
        if info.get("online"):
            connection.send_error(msg["id"], "online", "The device is online. Unplug it first, then remove it.")
            return
        hub.forget_device(dev_id)
        device = _ha_device(hass, dev_id, entry_id)
        if device is not None:
            dr.async_get(hass).async_update_device(device.id, remove_config_entry_id=entry_id)
        connection.send_result(msg["id"], {"removed": dev_id})
        return
    connection.send_error(msg["id"], "not_found", "Unknown ET-Core device")


@websocket_api.websocket_command({
    vol.Required("type"): "etcore/setup_info",
    vol.Optional("entry_id"): str,
})
@websocket_api.require_admin
@callback
def _ws_setup_info(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    """What a new device needs (the add-device guide). Admins only: it has the key."""
    hubs = _hubs(hass)
    hub = hubs.get(msg["entry_id"]) if "entry_id" in msg else next(iter(hubs.values()), None)
    if hub is None:
        connection.send_error(msg["id"], "not_set_up", "ET-Core is not set up")
        return
    connection.send_result(msg["id"], {
        "port": hub.port,
        "group": MULTICAST_GROUP,
        "ipv6": bool(getattr(hub, "_enable_ipv6", False)),
        "key": hub.psk_hex,
    })


@websocket_api.websocket_command({
    vol.Required("type"): "etcore/set_health_profile",
    vol.Required("entry_id"): str,
    vol.Required("dev_id"): str,
    vol.Required("profile"): vol.In(PROFILES),
})
@websocket_api.require_admin
@websocket_api.async_response
async def _ws_set_health_profile(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    hub = _hubs(hass).get(msg["entry_id"])
    if hub is None or msg["dev_id"] not in hub.devices:
        connection.send_error(msg["id"], "not_found", "Unknown device in this ET-Core connection")
        return
    await _load_profiles(hass)
    state = _profile_state(hass)
    async with state["lock"]:
        updated = {entry: dict(devices) for entry, devices in state["data"].items()}
        devices = updated.setdefault(msg["entry_id"], {})
        if msg["profile"] == "auto":
            devices.pop(msg["dev_id"], None)
        else:
            devices[msg["dev_id"]] = msg["profile"]
        try:
            await state["store"].async_save(updated)
        except Exception:
            _LOGGER.exception("Could not save ET-Core health profile")
            connection.send_error(msg["id"], "save_failed", "Profile was not saved; try again")
            return
        state["data"] = updated
    connection.send_result(msg["id"], {"profile": msg["profile"]})



@websocket_api.websocket_command({
    vol.Required("type"): "etcore/thread_bootstrap",
    vol.Required("entry_id"): str,
    vol.Required("dev_id"): vol.All(str, vol.Length(min=1, max=64)),
    vol.Required("session"): vol.All(str, vol.Match(r"^[0-9a-f]{14}$")),
    vol.Required("address"): str,
})
@websocket_api.require_admin
@callback
def _ws_thread_bootstrap(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    """Challenge a provisioned Thread peer from this hub's own IPv6 address.

    Does not admit a device or import state. The peer must prove its key and
    session using the ordinary encrypted handshake before it creates entities.
    """
    import ipaddress
    import time
    hub = (hass.data.get(DOMAIN) or {}).get(msg["entry_id"])
    if hub is None or not hub._psk or not hub._enable_ipv6:
        connection.send_error(msg["id"], "invalid_hub", "An encrypted IPv6 hub is required")
        return
    try:
        address = ipaddress.IPv6Address(msg["address"])
        if address.is_multicast or address.is_unspecified or address.is_link_local or address.is_loopback:
            raise ValueError
    except ValueError:
        connection.send_error(msg["id"], "invalid_address", "Use a routable unicast Thread IPv6 address")
        return
    limits = hass.data.setdefault("etcore_thread_bootstrap_limits", {})
    key = (msg["entry_id"], msg["dev_id"])
    now = time.monotonic()
    if key in limits and now - limits[key] < 10:
        connection.send_error(msg["id"], "rate_limited", "Wait ten seconds before another challenge")
        return
    limits[key] = now
    try:
        hub._send_announce(str(address), hello=True, dev_id=msg["dev_id"],
                           ds=msg["session"], ch=hub._make_challenge(msg["dev_id"], msg["session"], int(now)))
    except OSError:
        connection.send_error(msg["id"], "send_failed", "IPv6 challenge could not be sent")
        return
    connection.send_result(msg["id"], {"challenge_sent": True})


async def async_setup_panel(hass: HomeAssistant) -> None:
    await _load_profiles(hass)
    if not hass.data.get(f"{DOMAIN}_panel_loaded"):
        hass.http.register_view(EtCoreHtmlView)
        hass.http.register_view(EtCoreDiagApiView)
        from .thread_ota_view import ThreadOTAView
        hass.http.register_view(ThreadOTAView)
        for cmd in (_ws_diag, _ws_rename, _ws_remove, _ws_setup_info, _ws_set_health_profile, _ws_thread_bootstrap):
            websocket_api.async_register_command(hass, cmd)
        hass.data[f"{DOMAIN}_panel_loaded"] = True

    async_register_built_in_panel(
        hass,
        component_name="iframe",
        sidebar_title=PANEL_TITLE,
        sidebar_icon=PANEL_ICON,
        frontend_url_path=PANEL_URL_PATH,
        config={"url": VIEW_URL},
        require_admin=False,
        update=True,  # Multiple ET-Core entries share one dashboard.
    )


async def async_unload_panel(hass: HomeAssistant) -> None:
    try:
        async_remove_panel(hass, PANEL_URL_PATH)
    except Exception:
        pass
