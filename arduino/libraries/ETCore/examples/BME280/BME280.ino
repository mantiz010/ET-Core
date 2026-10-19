// ET-Core BME280 weather sensor (class sensor.weather): temperature (°C),
// humidity (%) and air pressure (hPa) as three Home Assistant sensors.
// Needs: "Adafruit BME280 Library" (+ "Adafruit Unified Sensor").
// Wiring (I2C): SDA -> SDA_PIN, SCL -> SCL_PIN, 3.3V, GND.
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

// The board's own I2C pins (ESP32: 21/22, ESP32-C3: 8/9). GPIO6-11 are the flash
// on a standard ESP32, so never hard-code 8/9 there.
#define SDA_PIN   SDA
#define SCL_PIN   SCL
#define EVERY_MS  30000

ETCore etcore;
Adafruit_BME280 bme;
bool found = false;

static void publish() {
  if (!found && !(found = bme.begin(0x76) || bme.begin(0x77))) {
    etcore.sendError("sensor", "BME280 not found at 0x76/0x77: check SDA/SCL");
    return;
  }
  JsonDocument d;
  d["temp"] = roundf(bme.readTemperature() * 10) / 10;
  d["humidity"] = roundf(bme.readHumidity() * 10) / 10;
  d["pressure"] = roundf(bme.readPressure() / 10.0f) / 10;  // Pa -> hPa, 1 decimal
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("weather1", "sensor.weather", "Weather Station", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
