// BMP280 barometer on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
Adafruit_BMP280 probe;
bool ready = false;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  if (!ready) {
    ready = probe.begin(0x76);
    if (!ready) node.device.sendError("sensor", "I2C probe initialization failed");
    return;
  }
  float value = probe.readPressure() / 100.0f;
  if (!isfinite(value) || value < 0) {
    ready = false;
    node.device.sendError("sensor", "Invalid measurement");
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
  if (!node.service("h2-bmp280barometer", "sensor.pressure", "BMP280 barometer")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
