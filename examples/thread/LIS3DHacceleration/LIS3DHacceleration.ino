// LIS3DH acceleration on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
Adafruit_LIS3DH probe;
bool ready = false;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  if (!ready) {
    ready = probe.begin(0x18);
    if (!ready) node.device.sendError("sensor", "Initialization failed");
    return;
  }
  float value = NAN;
  sensors_event_t a;
  if (probe.getEvent(&a)) value = a.acceleration.z;
  if (!isfinite(value)) {
    node.device.sendError("sensor", "Reading unavailable");
    return;
  }
  node.device.sendValue(value, "m/s²");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(requestReading);

  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-lis3dhacceleration", "sensor.acceleration", "LIS3DH acceleration")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
