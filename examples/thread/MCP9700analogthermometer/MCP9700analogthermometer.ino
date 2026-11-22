// MCP9700 analog thermometer on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>

ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant

uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  float mv = analogReadMilliVolts(1);
  if (mv < 10 || mv > 1900) {
    node.device.sendError("sensor", "Temperature output outside expected range");
    return;
  }
  node.device.sendValue((mv - 500) / 10.0f, "°C");
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(1, ADC_11db);
  node.device.onSync(requestReading);
  
  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-mcp9700analogthermom", "sensor.temperature", "MCP9700 analog thermometer")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
