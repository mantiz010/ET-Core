"""Native encrypted ET-Core audio player; devices report authoritative state."""
from __future__ import annotations

import json
import math
from typing import Any
from urllib.parse import urlsplit

from homeassistant.components import media_source
from homeassistant.components.media_player import (
    MediaPlayerDeviceClass, MediaPlayerEntity, MediaPlayerEntityFeature,
    MediaPlayerState, async_process_play_media_url,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from .const import DOMAIN
from .hub import EtCoreHub

DEVICE_CLASS = "media_player.audio"
MAX_COMMAND_BYTES = 768  # Reserve space for cmd_id, encryption and the 1500-byte RX envelope.
FEATURES = {
    "play_media": MediaPlayerEntityFeature.PLAY_MEDIA,
    "play": MediaPlayerEntityFeature.PLAY,
    "pause": MediaPlayerEntityFeature.PAUSE,
    "stop": MediaPlayerEntityFeature.STOP,
    "volume_set": MediaPlayerEntityFeature.VOLUME_SET | MediaPlayerEntityFeature.VOLUME_STEP,
    "power": MediaPlayerEntityFeature.TURN_ON | MediaPlayerEntityFeature.TURN_OFF,
}
STATES = {s.value: s for s in (MediaPlayerState.IDLE, MediaPlayerState.PLAYING,
                               MediaPlayerState.PAUSED, MediaPlayerState.BUFFERING, MediaPlayerState.OFF)}


def capabilities(info: dict[str, Any]) -> frozenset[str]:
    hello = info.get("hello")
    value = hello.get("media_capabilities") if isinstance(hello, dict) else None
    if (not isinstance(value, list) or len(value) > 16 or
            any(not isinstance(v, str) for v in value)):
        return frozenset()
    return frozenset(value) & (set(FEATURES) | {"tts"})


def finite_number(value: Any) -> float | None:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    try:
        number = float(value)
        return number if math.isfinite(number) else None
    except (ValueError, OverflowError):
        return None


def command(payload: dict[str, Any]) -> dict[str, Any]:
    """Reject, never truncate, commands which cannot fit the encrypted transport."""
    if len(json.dumps(payload, separators=(",", ":"), ensure_ascii=True).encode()) > MAX_COMMAND_BYTES:
        raise HomeAssistantError("ET-Core media command exceeds the device packet limit")
    return payload


def playable_url(value: str) -> str:
    try:
        parts = urlsplit(value)
        valid = (parts.scheme in {"http", "https"} and bool(parts.hostname)
                 and parts.username is None and parts.password is None
                 and len(value.encode()) <= 640
                 and not any(ord(c) < 32 or ord(c) == 127 for c in value))
    except ValueError:
        valid = False
    if not valid:
        raise HomeAssistantError("ET-Core audio requires an HTTP or HTTPS media URL")
    return value


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[str, EtCoreMediaPlayer] = {}

    @callback
    def receive(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        if (not dev_id or msg.get("class") != DEVICE_CLASS or
                msg.get("type") not in {"hello", "lease", "state"}):
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name") or (dev_id not in entities and not capabilities(info)):
            return  # A capability-bearing hello must precede entity creation.
        if dev_id not in entities:
            entities[dev_id] = EtCoreMediaPlayer(hub, dev_id, info)
            async_add_entities([entities[dev_id]])
        ent = entities[dev_id]
        ent.update_info(info)
        payload = msg.get("payload")
        if msg.get("type") == "state" and isinstance(payload, dict):
            ent.apply_state(payload)
        ent.refresh_availability()
        ent.write_if_added()

    @callback
    def status(data: dict[str, Any]) -> None:
        if ent := entities.get(str(data.get("id") or "")):
            ent.refresh_availability()
            ent.write_if_added()

    @callback
    def removed(dev_id: str) -> None:
        entities.pop(dev_id, None)

    hub.register_listener(receive)
    hub.register_status_listener(status)
    hub.register_removed_listener(removed)


class EtCoreMediaPlayer(MediaPlayerEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = None
    _attr_device_class = MediaPlayerDeviceClass.SPEAKER
    _attr_volume_step = 0.05

    def __init__(self, hub: EtCoreHub, dev_id: str, info: dict[str, Any]):
        self._hub, self._dev_id = hub, dev_id
        self._attr_unique_id = f"etcore_{dev_id}_media_player"
        self._attr_available = False
        self._boot = info.get("boot")
        self._caps = frozenset()
        self._attr_device_info = {"identifiers": {(DOMAIN, dev_id)},
                                  "name": info.get("name") or dev_id,
                                  "manufacturer": "ElectronicsTech",
                                  "model": str((info.get("hello") or {}).get("model") or "ET-Core Audio"),
                                  "sw_version": str(info.get("fw") or "")}
        self.clear_reported_state()
        self.update_info(info)

    def clear_reported_state(self) -> None:
        self._attr_state = None
        self._attr_volume_level = None
        self._attr_media_title = None
        self._attr_media_artist = None
        self._attr_media_content_id = None
        self._attr_media_content_type = None
        self._attr_media_duration = None
        self._attr_media_position = None
        self._attr_media_position_updated_at = None
        self._playback_error = None

    def update_info(self, info: dict[str, Any]) -> None:
        if info.get("boot") != self._boot:
            self._boot = info.get("boot")
            self.clear_reported_state()
        self._caps = capabilities(info)
        self._attr_supported_features = MediaPlayerEntityFeature(0)
        for key, flag in FEATURES.items():
            if key in self._caps:
                self._attr_supported_features |= flag
        self.refresh_availability()

    def refresh_availability(self) -> None:
        self._attr_available = (self._hub.devices.get(self._dev_id) or {}).get("online") is True

    def write_if_added(self) -> None:
        if self.hass is not None:
            self.async_write_ha_state()

    def apply_state(self, payload: dict[str, Any]) -> None:
        if isinstance(payload.get("state"), str) and payload["state"] in STATES:
            self._attr_state = STATES[payload["state"]]
        for key, attr, maximum in (("volume", "_attr_volume_level", 1),
                                   ("duration", "_attr_media_duration", None),
                                   ("position", "_attr_media_position", None)):
            value = payload.get(key)
            if value is None and key in payload:
                setattr(self, attr, None)
                if key == "position":
                    self._attr_media_position_updated_at = None
                continue
            number = finite_number(value)
            if number is not None and number >= 0 and (maximum is None or number <= maximum):
                setattr(self, attr, number)
                if key == "position":
                    self._attr_media_position_updated_at = dt_util.utcnow()
        for key, attr in (("title", "_attr_media_title"), ("artist", "_attr_media_artist"),
                          ("content_type", "_attr_media_content_type")):
            value = payload.get(key)
            if value is None and key in payload:
                setattr(self, attr, None)
            elif isinstance(value, str) and len(value.encode()) <= 256:
                setattr(self, attr, value or None)
        if "error" in payload:
            error = payload["error"]
            if error is None or error in ("stream_connect_failed", "tts_connect_failed"):
                self._playback_error = error
        if "url" in payload:
            value = payload["url"]
            if value is None:
                self._attr_media_content_id = None
            elif isinstance(value, str):
                try:
                    self._attr_media_content_id = playable_url(value)
                except HomeAssistantError:
                    pass

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        return {"playback_error": self._playback_error} if self._playback_error else {}

    def send(self, capability: str, payload: dict[str, Any]) -> None:
        self.refresh_availability()
        if not self._attr_available:
            raise HomeAssistantError("ET-Core media player is offline")
        if capability not in self._caps:
            raise HomeAssistantError("This ET-Core player does not advertise that control")
        self._hub.send_command(self._dev_id, DEVICE_CLASS, command(payload))
        # Transport ACK is not proof of playback; wait for the device's state.

    async def async_turn_on(self) -> None:
        self.send("power", {"op": "power", "on": True})

    async def async_turn_off(self) -> None:
        self.send("power", {"op": "power", "on": False})

    async def async_media_play(self) -> None:
        self.send("play", {"op": "play"})

    async def async_media_pause(self) -> None:
        self.send("pause", {"op": "pause"})

    async def async_media_stop(self) -> None:
        self.send("stop", {"op": "stop"})

    async def async_set_volume_level(self, volume: float) -> None:
        number = finite_number(volume)
        if number is None or not 0 <= number <= 1:
            raise HomeAssistantError("Volume must be a finite number between 0 and 1")
        self.send("volume_set", {"op": "volume", "volume": number})

    async def async_play_media(self, media_type: str, media_id: str, **kwargs: Any) -> None:
        if not isinstance(media_id, str) or not media_id:
            raise HomeAssistantError("Media must be a non-empty URL or TTS message")
        if media_type == "tts":
            if len(media_id.encode()) > 640 or "\x00" in media_id:
                raise HomeAssistantError("TTS text exceeds the device limit or contains a null character")
            extra = kwargs.get("extra") or {}
            language = extra.get("language", "en") if isinstance(extra, dict) else "en"
            if not isinstance(language, str) or not 1 <= len(language) <= 16 or not language.isascii() or not language.replace("-", "").isalpha():
                raise HomeAssistantError("Invalid TTS language")
            self.send("tts", {"op": "tts", "text": media_id, "language": language})
            return
        if media_source.is_media_source_id(media_id):
            resolved = await media_source.async_resolve_media(self.hass, media_id, target_media_player=self.entity_id)
            media_id = resolved.url
        url = async_process_play_media_url(self.hass, media_id)
        self.send("play_media", {"op": "play_media", "url": playable_url(url)})
