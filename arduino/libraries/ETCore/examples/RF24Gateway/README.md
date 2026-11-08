# ESP32-S3 RF24Network master over encrypted ET-Core

Replaces only the master's Wi-Fi/MQTT transport. Existing RF24 peer firmware,
packed packets, octal addresses, channel 90 and 1 Mbps stay the same. OLED SPI,
Serial debugging, Telnet and Arduino OTA remain available.

## Target and wiring

ESP32-S3 Dev Module, **4 MB flash, PSRAM disabled**, default OTA partition.
Arduino-ESP32 3.3.11; `USBMode=hwcdc,CDCOnBoot=default` (USB CDC disabled).
GPIO20 is the OLED reset, so this installation cannot also use it as native USB D+.
Serial debugging uses UART0. No onboard LEDs or other hardware are added.

| Function | GPIO |
|---|---:|
| SPI SCK / MOSI / MISO | 12 / 11 / 13 |
| NRF24 CE / CSN | 4 / 5 |
| OLED DC / CS / reset | 21 / 47 / 20 |

Both SPI devices use one initialized bus from one loop. Radio detection and OLED
allocation failures are logged and retried without stopping Wi-Fi/OTA. OLED
scrolling covers all 34 entries rather than overwriting a 29-slot buffer.

Libraries: RF24 1.4.8, RF24Network 2.0.0, Adafruit SSD1306 2.5.17,
Adafruit GFX 1.12.6, ArduinoJson 7.4.3, ETCore 0.4.3,
ETChaCha20Poly1305 1.0.0. Use installed library headers/examples for API authority.

## Installation

Copy `secrets.h.example` to `secrets.h`, supply Wi-Fi and the hub's 64-hex key,
and retain your current OTA password. The existing `NRF-Network` deployment was
confirmed to authenticate with the empty string: recent ArduinoOTA hashes even
an empty password and can therefore advertise `auth_upload=yes` for it.

Hostname stays `NRF-Network.local`, OTA port 3232, Telnet port 23.
Serial and Telnet status show the IP, build, radio status and packet counters.
The normal build does not include or link an MQTT client.

Use the accompanying native `bridge.rf24` / `rf24_network_v1` HA adapter. Existing
ESP-NOW bridge support is retained. Its schema hash binds the exact 34-channel
profile; mismatched versions do not silently create conflicting entities.

## Preserved protocol and switch behavior

Sensor addresses: `01,02,03,04,05,033,034,011,013` (octal). Switch destination:
`012`. All nine packed layouts have compile-time size checks. Integers are
explicitly 32-bit where the original ESP32 used `int`. Frames are drained before
validation, including unknown/truncated/oversized frames, so a bad sender cannot
pin the receive loop on one packet.

A switch command is a single byte, exactly as in the existing node. No switch
command is sent at boot or during deployment verification. Before an explicit
successful send, the command state is unknown. A successful `network.write`
confirms transmission, **not physical actuator feedback**. No new node ACK
protocol is invented. The separate MQTT Node 4 relay is outside the supplied
master's command handlers and remains untouched.

Measurements are unknown until real packets arrive. Sample ages survive state
snapshots/reconnects, and readings expire after 30 minutes without a new frame.
No boot zeroes are published. Peer nodes that were already offline are not
reported as migration failures. This freshness policy is stricter than legacy
MQTT sensors that retained values indefinitely.

## Home Assistant migration and rollback

Back up active MQTT YAML, entity/device registries, customization and the original
sketches before changes. This installation maps **33 sensor IDs and one switch ID**
to native ET-Core entities, preserving units/names/history; mappings are specific
to the existing home. Retire only their exact MQTT entries after shadow readings
are verified. Leave unrelated MQTT devices and the separate Node 4 relay alone.

An optional `RF24_LEGACY_MQTT` build uses the same verified RF24/OLED implementation
with the old topics and one switch command. Define that macro before the includes
and provide the optional broker fields to build a recovery application. This is a
reconstructed MQTT recovery image, **not a readback of the original running flash**.
The actual original flash image cannot be retrieved through ArduinoOTA. Private
local source/configuration backups are kept separately; do not publish compiled
images because they contain Wi-Fi and encryption credentials.

## Verification

Host tests exercise the actual packet decoder, all node layouts, invalid values,
wrong lengths/addresses and 100,000 malformed frames under ASan/UBSan. HA tests
cover the RF24 adapter and the existing ESP-NOW behavior. The exact S3 build is
required in addition to these tests. Physical verification must observe real RF
packets and encrypted HA states; compilation alone does not prove the hardware.
