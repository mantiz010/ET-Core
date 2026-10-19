// ET-Core SCD40 / SCD41 CO2 sensor (class sensor.air): CO2 (ppm), temperature
// and humidity. Real NDIR CO2, good for "open a window" automations.
// Needs: "Sensirion I2C SCD4x". Wiring (I2C): SDA, SCL, 3.3V, GND.
#include <WiFi.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

// The board's own I2C pins (ESP32: 21/22, ESP32-C3: 8/9). GPIO6-11 are the flash
// on a standard ESP32, so never hard-code 8/9 there.
#define SDA_PIN   SDA
#define SCL_PIN   SCL

ETCore etcore;
SensirionI2cScd4x scd;

static void publish() {
  bool ready = false;
  if (scd.getDataReadyStatus(ready) != 0) { etcore.sendError("sensor", "SCD4x not answering: check SDA/SCL"); return; }
  if (!ready) return;
  uint16_t co2 = 0; float t = 0, h = 0;
  if (scd.readMeasurement(co2, t, h) != 0 || co2 == 0) return;
  JsonDocument d;
  d["co2"] = co2;
  d["temp"] = roundf(t * 10) / 10;
  d["humidity"] = roundf(h * 10) / 10;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  scd.begin(Wire, SCD40_I2C_ADDR_62);
  scd.stopPeriodicMeasurement();   // in case it was running before a reset
  delay(500);
  scd.startPeriodicMeasurement();  // a new reading every 5 s
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("co2sensor1", "sensor.air", "CO2 Sensor", "1.0");
  etcore.onSync(publish);
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > 30000) { last = millis(); publish(); }
}
