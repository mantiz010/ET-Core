# Porting an ET-Bus sketch to ET-Core

ET-Core uses the same device classes and payload keys as ET-Bus 1.7, so a sketch
ports by swapping library calls. The key is derived the same way
(`sha256(psk || device_id)`), so the same 64-hex PSK works.

| ET-Bus | ET-Core |
|---|---|
| `#include <ETBus.h>` / `ETBus etbus;` | `#include <ETCore.h>` / `ETCore etcore;` |
| `etbus.begin(id, class, name, fw)` | `etcore.begin(id, class, name, fw)` (same) |
| `etbus.enableEncryptionHex(psk)` | same, call before `begin()` |
| `etbus.onCommand(cb)` | same; the library sends the ack for you |
| `etbus.onSync(cb)` | same (called when Home Assistant restarts and asks) |
| `etbus.sendState(p)` | same |
| `etbus.sendSwitchState(on)` | same |
| `etbus.sendRgbStateFx(on, r, g, b, bri, effect, speed)` | same |
| `etbus.sendError(code, msg)` | same (encrypted; HA logs it and fires `etcore_device_error`) |
| `etbus.setPort(p)` | same (default 5560; must match the integration's port) |
| `etbus.sendDiscover(extra)` | `etcore.onHello(cb)`: add fields to the hello (effects, switches, presets) |
| `etbus.sendPong()` / ping handling | nothing: the library sends leases itself |
| `ETBusWiFiManager` | `ETCoreWiFiManager` (same API, same saved settings) |
| `StaticJsonDocument<N>` / `containsKey` | `JsonDocument` / `payload["k"].is<T>()` (ArduinoJson 7) |

New in ET-Core: `sendSwitchesState(states, n)`, `sendFanSpeed(on, pct)`,
`sendFanPreset(on, preset)`, `sendValue(value, unit)`, `sendBye()` (before OTA or
restart), `setLeaseInterval/setLeaseTtl`, `setHubSilentTimeout`.

## Device types (same as ET-Bus)

| Class | State | Command | Hello (optional) |
|---|---|---|---|
| `switch.<anything>` | `{"on": true}` | `{"on": true}` | |
| `switch.multi` (any class with a switch list) | `{"switches": {"1": true, "2": false}}` | `{"switch_id": "1", "on": true}` | `"switches": [{"id": "1", "name": "Pump"}, ...]` |
| `light.rgb` | `{"on", "r", "g", "b", "brightness", "effect", "speed"}` | same keys (only the changed ones) | `"effects": [...]` (default: ET-Bus's 35) |
| `fan.speed` | `{"on", "speed": 0-100}` | `{"on", "speed"}` | |
| `fan.preset` | `{"on", "preset"}` | `{"on", "preset"}` | `"presets": [...]` (default off/low/medium/high) |
| `sensor.<metric>` | `{"value": 21.5, "unit": "°C"}` | | |
| `sensor.<anything>` | `{"temp": 21.5, "humidity": 48, "co2": 600, ...}` | | |

Sensors with a known name get Home Assistant's device class and unit: temp
(temperature), humidity, co2, pressure, pm25, pm10, lux, voltage, current, power,
battery. Others show the value as sent (with `unit` if given).

## Differences on purpose

- Home Assistant shows what the device **reports**: after a command the state
  changes when the device confirms it (~25 ms), and after an HA restart the device
  is asked for its real state (shown in ~1-2 s). ET-Bus shows a command's result
  before the device answers, and a saved state after a restart even if the device
  changed or is gone.
- Light effect speed has its own slider (`number.<light>_effect_speed`):
  Home Assistant's light service has no speed field.
- The integration's port and key can be changed in its settings (it reloads).

## Not ported

- `etbus.send_kate_command` (Kate AI talks ET-Bus; moving Kate is a separate job).
- Showing a saved state while a device is offline (see above).
