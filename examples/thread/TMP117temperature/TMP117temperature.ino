// TMP117 temperature on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <Wire.h>
#include <7Semi_TMP11x.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
TMP11x_7Semi probe;
bool ready = false;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  if (!ready) {
    ready = probe.begin(0x48);
    if (!ready) node.device.sendError("sensor", "Initialization failed");
    return;
  }
  float value = NAN;
  if (!probe.readTemperatureC(value)) value = NAN;
  if (!isfinite(value)) {
    node.device.sendError("sensor", "Reading unavailable");
    return;
  }
  node.device.sendValue(value, "°C");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(requestReading);

  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-tmp117temperature", "sensor.temperature", "TMP117 temperature")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
