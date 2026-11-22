// Plunger microswitch on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>

ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
bool raw = false, stable = false, initialized = false;
uint32_t changedAt = 0;
void publish() { node.device.sendSwitchState(stable); }

void setup() {
  Serial.begin(115200);
  pinMode(4, INPUT_PULLUP);
  stable = raw = digitalRead(4) == LOW;
  node.device.onSync(publish);
  
  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-plungermicroswitch", "binary_sensor.opening", "Plunger microswitch")) return;
  bool active = digitalRead(4) == LOW;
  if (active != raw) { raw = active; changedAt = millis(); }
  if (millis() - changedAt >= 35 && (!initialized || raw != stable)) {
    initialized = true;
    stable = raw;
    publish();
  }
}
