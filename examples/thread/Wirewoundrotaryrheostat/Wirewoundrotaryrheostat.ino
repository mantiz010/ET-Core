// Wirewound rotary rheostat on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>

ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
constexpr float excitationMv = 2000;
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  float mv = analogReadMilliVolts(1);
  if (mv >= excitationMv - 5) {
    node.device.sendError("range", "Open circuit or resistance above range");
    return;
  }
  node.device.sendValue(1000.0f * mv / (excitationMv - mv), "Ω");
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(1, ADC_11db);
  node.device.onSync(requestReading);
  
  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-wirewoundrotaryrheos", "sensor.resistance", "Wirewound rotary rheostat")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
