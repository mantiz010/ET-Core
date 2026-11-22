// MPU6050 rotation on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
Adafruit_MPU6050 probe;
bool ready = false;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  if (!ready) {
    ready = probe.begin();
    if (!ready) node.device.sendError("sensor", "Initialization failed");
    return;
  }
  float value = NAN;
  sensors_event_t a, g, t;
  if (probe.getEvent(&a, &g, &t)) value = g.gyro.z;
  if (!isfinite(value)) {
    node.device.sendError("sensor", "Reading unavailable");
    return;
  }
  node.device.sendValue(value, "rad/s");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(requestReading);

  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-mpu6050rotation", "sensor.rotation", "MPU6050 rotation")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
