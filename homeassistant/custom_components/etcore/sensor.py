from __future__ import annotations

from typing import Any

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .hub import EtCoreHub

# Known readings: (name, device class, unit). Units are the plain strings HA's unit
# enums stand for, so this works across HA versions (ET-Bus's
# CONCENTRATION_PARTS_PER_MILLION is deprecated in 2026.10).
_KNOWN: dict[str, tuple[str, SensorDeviceClass | None, str | None]] = {
    "temp": ("Temperature", SensorDeviceClass.TEMPERATURE, "°C"),
    "humidity": ("Humidity", SensorDeviceClass.HUMIDITY, "%"),
    "co2": ("CO2", SensorDeviceClass.CO2, "ppm"),
    "pressure": ("Pressure", SensorDeviceClass.PRESSURE, "hPa"),
    "pm25": ("PM2.5", SensorDeviceClass.PM25, "μg/m³"),
    "pm10": ("PM10", SensorDeviceClass.PM10, "μg/m³"),
    "lux": ("Illuminance", SensorDeviceClass.ILLUMINANCE, "lx"),
    "voltage": ("Voltage", SensorDeviceClass.VOLTAGE, "V"),
    "current": ("Current", SensorDeviceClass.CURRENT, "A"),
    "power": ("Power", SensorDeviceClass.POWER, "W"),
    "battery": ("Battery", SensorDeviceClass.BATTERY, "%"),
}
# Other spellings devices use (ET-Bus normalises "temperature" to "temp").
_ALIASES = {"temperature": "temp", "rh": "humidity", "pm2_5": "pm25", "illuminance": "lux"}
# Payload fields that are not readings.
_SKIP = {"unit", "name", "fw", "lib", "features"}


def _metric(key: str) -> str:
    k = str(key).lower()
    return _ALIASES.get(k, k)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    """sensor.* devices, ET-Bus payloads: {"value": 21.5, "unit": "°C"} (metric from the
    class, e.g. sensor.temperature) or several readings {"co2": 600, "temp": 21.5}."""
    hub: EtCoreHub = hass.data[DOMAIN][entry.entry_id]
    entities: dict[tuple[str, str], EtCoreSensor] = {}

    @callback
    def handle_message(msg: dict[str, Any]) -> None:
        dev_id = str(msg.get("id") or "")
        dev_class = str(msg.get("class") or "")
        payload = msg.get("payload") if isinstance(msg.get("payload"), dict) else {}
        if not dev_id or not dev_class.startswith("sensor."):
            return
        # Only state carries readings; lease/ack/hello fields are not sensors.
        if msg.get("type") != "state":
            return
        info = hub.devices.get(dev_id, {})
        if not info.get("name"):
            return

        if "value" in payload:
            readings = {_metric(dev_class.split(".", 1)[1] or "value"): payload["value"]}
        else:
            readings = {_metric(k): v for k, v in payload.items() if k not in _SKIP}
        unit = payload.get("unit") if isinstance(payload.get("unit"), str) else None

        new = []
        for metric, value in readings.items():
            if value is None or not isinstance(value, (int, float, str, bool)):
                continue
            key = (dev_id, metric)
            if key not in entities:
                entities[key] = EtCoreSensor(hub, dev_id, metric, info, unit if len(readings) == 1 else None)
                new.append(entities[key])
            entities[key].set_value(value)
        if new:
            async_add_entities(new)

    @callback
    def handle_status(data: dict[str, Any]) -> None:
        dev_id = str(data.get("id") or "")
        for (did, _), ent in list(entities.items()):
            if did == dev_id:
                ent.async_write_ha_state()

    hub.register_listener(handle_message)
    hub.register_status_listener(handle_status)

    @callback
    def handle_removed(dev_id: str) -> None:
        # Device removed in HA: forget our entities so they are created again if it returns.
        for key in [k for k in entities if (k[0] if isinstance(k, tuple) else k) == dev_id]:
            entities.pop(key)

    hub.register_removed_listener(handle_removed)


class EtCoreSensor(SensorEntity):
    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, hub: EtCoreHub, dev_id: str, metric: str, info: dict[str, Any], unit: str | None):
        self._hub = hub
        self._dev_id = dev_id
        self._metric = metric
        name, dclass, known_unit = _KNOWN.get(metric, (metric.replace("_", " ").capitalize(), None, None))
        self._attr_name = name
        self._attr_device_class = dclass
        self._attr_native_unit_of_measurement = known_unit or unit
        self._attr_unique_id = f"etcore_{dev_id}_{metric}"
        self._attr_native_value = None
        self._attr_device_info = {
            "identifiers": {(DOMAIN, dev_id)},
            "name": info.get("name") or dev_id,
            "manufacturer": "ElectronicsTech",
            "model": str((info.get("hello") or {}).get("model") or "ET-Core Sensor"),
            "sw_version": str(info.get("fw") or ""),
        }

    @property
    def available(self) -> bool:
        return bool((self._hub.devices.get(self._dev_id) or {}).get("online", False))

    def set_value(self, value: Any) -> None:
        self._attr_native_value = value
        # Numbers are measurements (history graphs, statistics); text is not.
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            self._attr_state_class = SensorStateClass.MEASUREMENT
        if self.hass is not None:  # not yet added: HA writes it when it is
            self.async_write_ha_state()
