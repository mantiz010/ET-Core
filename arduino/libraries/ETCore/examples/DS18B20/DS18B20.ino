// ET-Core DS18B20 waterproof temperature probe (class sensor.temperature).
// One reading in °C. Good for water tanks, aquariums, freezers, pipes.
// Needs: "OneWire" and "DallasTemperature". Wiring: DATA -> ONEWIRE_PIN with a
// 4.7k pull-up to 3.3V.
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define ONEWIRE_PIN  4
#define EVERY_MS     30000

ETCore etcore;
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature probes(&oneWire);

static void publish() {
  probes.requestTemperatures();
  float t = probes.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    etcore.sendError("sensor", "DS18B20 not found: check wiring and the 4.7k pull-up");
    return;
  }
  etcore.sendValue(roundf(t * 10) / 10, "°C");
}

void setup() {
  Serial.begin(115200);
  probes.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("probe1", "sensor.temperature", "Water Temperature", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
