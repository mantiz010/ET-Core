// Inclinometer on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>

ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
constexpr float spanLow = -45, spanHigh = 45;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  float millivolts = analogReadMilliVolts(1);
  if (millivolts < 380 || millivolts > 2050) {
    node.device.sendError("loop", "4-20 mA input outside calibrated span");
    return;
  }
  float value = spanLow + (millivolts - 400) * (spanHigh - spanLow) / 1600;
  node.device.sendValue(value, "°");
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(1, ADC_11db);
  node.device.onSync(requestReading);

  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-inclinometer", "sensor.measurement", "Inclinometer")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
