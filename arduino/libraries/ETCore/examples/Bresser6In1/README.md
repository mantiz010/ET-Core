# Bresser 6-in-1 · encrypted ET-Core receiver

Standard ESP32 plus CC1101, receiving Bresser 6-in-1 RF packets and reporting to Home Assistant. No MQTT dependency, TCP broker connection or broker credentials.

## Hardware and RF

| CC1101 signal | ESP32 GPIO |
| --- | --- |
| CS | 5 |
| GDO0 | 22 |
| GDO2 | 4 |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |

Use 3.3 V logic and the correct CC1101 module/antenna. GPIO5 is an ESP32 strapping pin: these are the validated existing connections; confirm your board's boot requirements before wiring another receiver. Keep the existing wiring when upgrading the tested receiver.

The tested station transmits at **917.0 MHz** in New Zealand. This is not a universal Bresser frequency: confirm the frequency printed on your own station. Radio settings retained from the working receiver: 8.22 kbps, 57.136417 kHz deviation, 270 kHz RX bandwidth, fixed 27-byte packets and sync word `AA 2D`.

## Install

1. Install the ESP32 board package and the repository's **ETCore** and **ETChaCha20Poly1305** libraries. Also install **ArduinoJson 7.4.3** and **RadioLib 6.6.0**. ArduinoOTA is supplied by the ESP32 core. The exact tested core is **Arduino-ESP32 3.3.11**; other versions have not been verified for this example.
2. Install the repository's Home Assistant integration **0.4.4 or newer** for weather units and the `OK` / `LOW` battery display. If no release includes this version yet, copy `homeassistant/custom_components/etcore` from the downloaded repository into `/config/custom_components/etcore` and restart Home Assistant.
3. Generate your own 64-character encryption key using [`ETCore-Key-Generator.html`](../../../../../ETCore-Key-Generator.html) or `python3 tools/generate_key.py`. Configure that same key in the Home Assistant ET-Core integration. Keep the integration's existing key if you already have devices: changing it requires updating those devices too.
4. Open `Bresser6In1.ino` in Arduino IDE. Edit **BresserConfig.h** locally with your Wi-Fi SSID, password and encryption key. Port **5560** must match the integration. Do not share the edited header or compiled binary.
5. Choose **ESP32 Dev Module**, 4 MB flash, default OTA-capable partition and PSRAM disabled. Exact compile target: `esp32:esp32:esp32:FlashSize=4M,PartitionScheme=default,PSRAM=disabled`. Confirm those settings match your actual board before uploading.
6. Upload using USB for a fresh board. An existing compatible receiver with Arduino OTA can be updated at `bresser-6in1.local`, port **3232**. Keep a backup and access to USB before updating. This example preserves the original OTA and Telnet authentication behaviour; they are local services and are not encrypted by the ET-Core key.
7. Home Assistant should discover **Bresser Weather**. Confirm live readings and battery state before removing any previous integration.

The checked-in key is intentionally invalid. Telemetry stays disabled if the key fails validation; there is no plaintext fallback. Serial is **115200 baud**, Telnet is TCP **23**, and the OTA hostname remains **bresser-6in1**. ET-Core device ID is `bresser-6in1`; use a unique device ID and OTA hostname if you have multiple receivers.

## Features retained

- Temperature (°C), humidity (%), gust/speed (m/s), direction (°), total rainfall (mm), battery (`OK`/`LOW`) and RF sensor ID.
- Digest/checksum validation, normal/rain-frame separation, two-sample battery debounce and valid zero rainfall reporting.
- Interrupt-driven CC1101 reception, retry on radio initialization failure and responsive Wi-Fi recovery, without automatic device restarts.
- OTA pauses RF reception and resumes after failure. Telnet diagnostics close slow clients rather than blocking the receiver.
- Encrypted ET-Core state updates, periodic refresh and re-sync after Home Assistant returns. Only readings less than two minutes old are re-sent. Missing readings are omitted rather than fabricated. UDP send is not an application acknowledgement; live reception must be checked in Home Assistant.

The receiver accepts valid supported station packets without a station-ID filter. If another Bresser is in RF range, check the reported ID before relying on the readings. ET-Core device IDs identify receivers; they are separate from RF station IDs.

## Keep existing Home Assistant entity IDs

A fresh installation gets new entities. Existing MQTT entity IDs are **not migrated automatically**. Back up your configuration, let ET-Core create its sensors, verify the values, then remove only the old Bresser MQTT definitions and reload MQTT. Rename the ET-Core entities in Home Assistant to the exact previous IDs. Keep existing automations, dashboards, history helpers and units. `battery_ok` is a true/false wire value displayed as `OK`/`LOW`; `temperature` is normalized to `temp` for the ET-Core unique ID.

A historical retained MQTT rainfall total may be older than the live RF total. Switching can produce a temporary spike in a rolling change helper; check it before using rainfall notifications or irrigation automations.

## Verification

The deployed source this example derives from was OTA-tested on a standard ESP32 with a 917 MHz station: encrypted state and lease reception, live weather data, OTA advertisement, Telnet and recovery after a Home Assistant restart were verified. The shareable example is separately compiled for the exact target above. This is evidence for that hardware, not every Bresser model or RF region. Decoder tests cover reference packets, corrupt/truncated rejection, battery debounce, high-bit IDs, zero rain and frame separation.

To run the portable decoder, key-generator and weather-entity regression tests from the repository root:

```sh
python3 -m unittest tools/test_generate_key.py tools/test_weather_sensors.py tools/test_bresser_decoder.py -v
```

The decoder test requires `g++`; Python tests use the standard library. These checks do not replace the exact ESP32 compile or hardware validation.
