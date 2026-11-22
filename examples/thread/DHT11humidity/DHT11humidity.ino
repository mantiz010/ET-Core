// DHT11 humidity on ESP32-H2 Dev Module. See README.md for wiring and testing.
#include <ETCoreThread.h>
#include <DHT.h>
ETCoreThread node;
const char* key = "";       // The integration's 64-hex-character key
const char* hub = "";       // Routable IPv6 address of Home Assistant
DHT probe(4, DHT11);
uint32_t lastRead = 0;
bool requested = true;
void requestReading() { requested = true; }
void publish() {
  float value = probe.readHumidity();
  if (isfinite(value)) node.device.sendValue(value, "%");
  else node.device.sendError("sensor", "DHT read failed");
}

void setup() {
  Serial.begin(115200);
  probe.begin();
  node.device.onSync(requestReading);

  node.configure(key, hub);
}

void loop() {
  if (!node.service("h2-dht11humidity", "sensor.humidity", "DHT11 humidity")) return;
  if (requested || millis() - lastRead >= 5000) {
    requested = false;
    lastRead = millis();
    publish();
  }
}
