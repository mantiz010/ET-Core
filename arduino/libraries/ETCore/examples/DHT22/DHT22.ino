// ET-Core DHT22 / AM2302 temperature + humidity (class sensor.climate).
// Home Assistant gets two sensors with units: Temperature (°C) and Humidity (%).
// If the sensor stops answering, the device reports an error (shown on the
// ET-Core page and as the etcore_device_error event).
// Needs: "DHT sensor library" by Adafruit. Wiring: DATA -> DHT_PIN, 10k pull-up.
#include <WiFi.h>
#include <DHT.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define DHT_PIN    4
#define DHT_TYPE   DHT22   // or DHT11
#define EVERY_MS   30000

ETCore etcore;
DHT dht(DHT_PIN, DHT_TYPE);

static void publish() {
  float t = dht.readTemperature(), h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    etcore.sendError("sensor", "DHT not answering: check wiring and the pull-up");
    return;
  }
  JsonDocument d;
  d["temp"] = roundf(t * 10) / 10;
  d["humidity"] = roundf(h * 10) / 10;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("climate1", "sensor.climate", "Room Climate", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
