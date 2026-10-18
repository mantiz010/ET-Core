// ET-Core soil moisture (class sensor.moisture): % wet, from a capacitive soil
// sensor (v1.2 / v2.0). Use it to water plants automatically.
// Calibrate: note the raw reading in dry air (DRY_RAW) and in a glass of water
// (WET_RAW); the serial monitor prints it every reading.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define SENSOR_PIN  3       // an ADC pin
#define DRY_RAW     3000
#define WET_RAW     1200
#define EVERY_MS    300000  // 5 minutes

ETCore etcore;

static void publish() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(SENSOR_PIN); delay(5); }
  int raw = sum / 16;
  float pct = constrain((float)(DRY_RAW - raw) / (DRY_RAW - WET_RAW) * 100.0f, 0.0f, 100.0f);
  Serial.printf("raw %d -> %.0f %%\n", raw, pct);
  etcore.sendValue(roundf(pct), "%");
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("soil1", "sensor.moisture", "Plant Soil", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
