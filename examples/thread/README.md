# ET-Core hardware projects over Thread

[Browse all 120 projects](index.md). Each folder contains a complete Arduino sketch, its build options, wiring and a bench check. Start with [BME280 weather](BME280Weather/README.md) for reporting or [low-voltage relay](LowVoltageRelay/README.md) for control. The collection includes I²C chips, passive contacts, resistive sensors, 4–20 mA transmitter profiles and driven outputs. Choose a profile by the device's electrical interface and nameplate range.

## Target and setup

Use an ESP32-H2 Dev Module with 4 MB flash and Arduino-ESP32 3.3.11. Use ETCore and ETChaCha20Poly1305 from this branch, plus ArduinoJson 7. Install the sensor driver identified in the individual project. The target is `esp32:esp32:esp32h2:PartitionScheme=huge_app`. Keep the sketch directory intact so Arduino picks up its OpenThread datagram buffer option.

1. Commission the ESP32-H2 on your existing Thread network. One route is the Arduino OpenThread library's Native → ThreadCommissioning → JoinerNode sketch with your commissioner and private joiner passcode. Keep the active dataset in NVS when uploading the project. The node checks for a saved dataset at startup.
2. Configure the ET-Core integration from the Thread transport branch with IPv6 enabled and a 64-hex-character ET-Core key. Use a routable unicast IPv6 address on the Home Assistant host reachable through the border router. Device and hub use UDP 5560 by default. For a different port call `node.device.setPort(port)` before `node.configure`. See the [transport notes](../../THREAD-TRANSPORT-PREVIEW.md).
3. Enter the key and hub address locally in the sketch. Use a unique device ID when deploying more than one copy. Keep private values out of Git.
4. From the repository root, compile, for example, with `arduino-cli compile --fqbn esp32:esp32:esp32h2:PartitionScheme=huge_app --libraries arduino/libraries examples/thread/BME280Weather`. Upload through your normal USB workflow and open Serial at 115200.
5. Look for the named device in ET-Core. Perform its bench check. For a switch, confirm the physical output, reported state and command acknowledgement. Power-cycle and check that the node rejoins and reports again.

## Electrical interfaces

The sketches use one project per board. On this H2 variant I²C uses GPIO12 and GPIO22, analog inputs use GPIO1, and contact or enable inputs use GPIO4. Use a 3.3 V logic interface. Powered loads require an appropriate protected driver and their own rated supply. For analog field transmitters use the isolated receiver and calibration values specified by the example. The transmitter profiles assume a linear output and require the actual nameplate span. Passive resistance projects publish ohms for calibration against the selected element.

The reference builds exercise firmware compilation. Physical wiring, calibration, relay polarity and end-to-end functionality need bench verification with the specific hardware.
