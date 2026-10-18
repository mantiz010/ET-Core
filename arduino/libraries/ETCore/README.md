# ETCore library

ET-Core connects ESP32 devices to Home Assistant over your local network:
encrypted (ChaCha20-Poly1305), fast (a few ms per command) and with no cloud or
MQTT broker. Install the **ET-Core** integration in Home Assistant, give every
device the same 64-character network key, and they appear on their own.

ET-Core is for **powered** devices. A device checks in every 2 seconds so Home
Assistant knows within seconds when it goes away, which keeps WiFi on all the
time (about 80-120 mA). Battery sensors that deep-sleep need Zigbee or ESP-NOW.

## Install (Arduino IDE)

1. *Sketch → Include Library → Add .ZIP Library…* and pick `ETChaCha20Poly1305.zip`,
   then `ETCore.zip` (and `ETCoreWiFiManager.zip` if you want the WiFi setup portal).
2. From the Library Manager install **ArduinoJson** (7.x), plus the library an
   example names at its top (FastLED, DHT sensor library, ...).
3. Board: any ESP32 (ESP32, ESP32-C3, ESP32-S3) with the esp32 core 3.x.
4. Open *File → Examples → ETCore*, put your WiFi and network key at the top of
   the sketch, set a unique device id, upload.

The network key is on the ET-Core page in Home Assistant (*Add device*).

## Examples

| Example | Class | In Home Assistant |
|---|---|---|
| **Blink** | `switch.led` | The board's LED as a switch. Start here. |
| **RelayOne** | `switch.relay` | One relay. |
| **RelayButton** | `switch.relay` | Relay + wall button + status LED, state kept over power cuts. |
| **MultiRelay** | `switch.multi` | 4-relay board, one switch per relay. |
| **Dimmer** | `light.dimmer` | One PWM channel with smooth fades: brightness only. |
| **RGBLight** | `light.rgb` | RGB LED on 3 PWM pins. |
| **WS2812** | `light.rgb` | WS2812/NeoPixel strip with **52 effects** (all 35 ET-Bus effects + 17 more) and an effect speed slider. |
| **Fan** | `fan.speed` | PWM fan, 0-100 %. |
| **FanPreset** | `fan.preset` | 3-speed fan on relays: Low / Medium / High. |
| **AirSensor** | `sensor.air` | CO2 / temperature / humidity template. |
| **DHT22** | `sensor.climate` | Temperature + humidity. |
| **DS18B20** | `sensor.temperature` | Waterproof temperature probe. |
| **BME280** | `sensor.weather` | Temperature, humidity, pressure. |
| **SCD40** | `sensor.air` | Real NDIR CO2 + temperature + humidity. |
| **BH1750** | `sensor.lux` | Light level in lux. |
| **INA219** | `sensor.power` | DC voltage, current, power. |
| **EnergyMeter** | `sensor.energy` | Pulses from your power meter: W and kWh (Energy dashboard). |
| **TankLevel** | `sensor.tank` | Ultrasonic water tank: distance and % full. |
| **SoilMoisture** | `sensor.moisture` | Capacitive soil sensor, % wet. |
| **BatteryMonitor** | `sensor.battery` | 12 V battery bank voltage and %. |
| **MotionPIR** | `binary_sensor.motion` | PIR / radar motion: Detected / Clear. |
| **DoorContact** | `binary_sensor.door` | Reed switch: Open / Closed. |
| **WaterLeak** | `binary_sensor.moisture` | Leak probe: Wet / Dry, with a problem alert. |
| **Doorbell** | `binary_sensor.button` | A press for automations (chime, notification). |

## Device classes

- `switch.*` — `{"on": bool}`; `switch.multi` lists `switches` in the hello.
- `light.rgb` — `on`, `r`/`g`/`b`, `brightness`, `effect`, `speed`; effects listed in the hello.
- `light.dimmer` (or `light.white`) — `on`, `brightness`.
- `fan.speed` — `on`, `speed` 0-100. `fan.preset` — `on`, `preset`; presets listed in the hello.
- `sensor.*` — `{"value": x, "unit": "..."}` for one reading, or several named readings:
  `temp`, `humidity`, `pressure`, `co2`, `pm25`, `pm10`, `lux`, `voltage`, `current`,
  `power`, `energy`, `battery`, `distance`, `moisture`, `level`, `rssi`, `voc`, `gas`.
- `binary_sensor.<kind>` — `{"on": bool}`; kind: motion, occupancy, presence, door,
  window, garage_door, opening, contact, moisture, leak, smoke, gas, vibration, sound,
  light, power, problem, button.

Commands are acknowledged by the library for you. `sendError(code, message)`
shows a problem on the ET-Core page and fires `etcore_device_error` in Home
Assistant.
