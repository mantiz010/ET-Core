// ET-Core air sensor (class sensor.air), the ET-Bus AirSensor example ported:
// CO2, temperature and humidity become three Home Assistant sensors with units.
// Replace the random values with your sensor readings.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same key as in Home Assistant

ETCore etcore;

static void publishReadings() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["co2"] = random(400, 900);              // ppm
  p["temp"] = random(200, 300) / 10.0;      // °C
  p["humidity"] = random(400, 700) / 10.0;  // %
  etcore.sendState(p);
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  etcore.begin("air1", "sensor.air", "Air Sensor 1", "air-1.0");
  etcore.onSync(publishReadings);
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > 30000) {  // every 30 s; the lease keeps it online in between
    last = millis();
    publishReadings();
  }
}
