// BME680 barometer on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
Adafruit_BME680 probe;
bool ready = false;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  if (!ready) {
    ready = probe.begin(0x77);
    if (!ready) node.device.sendError("sensor", "Initialization failed");
    return;
  }
  float value = NAN;
  if (probe.performReading()) value = probe.pressure / 100.0f;
  if (!isfinite(value)) {
    node.device.sendError("sensor", "Reading unavailable");
    return;
  }
  node.device.sendValue(value, "hPa");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(requestReading);
  
  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-bme680barometer", "sensor.pressure", "BME680 barometer")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
