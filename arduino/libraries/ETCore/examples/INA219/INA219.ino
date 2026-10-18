// ET-Core INA219 DC power monitor (class sensor.power): voltage (V), current (A)
// and power (W). For solar panels, batteries, 12 V systems, USB loads.
// Needs: "Adafruit INA219". Wiring (I2C): SDA, SCL, 3.3V, GND; the load's
// positive wire goes through VIN+ -> VIN-.
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define SDA_PIN   8
#define SCL_PIN   9
#define EVERY_MS  10000

ETCore etcore;
Adafruit_INA219 ina;
bool found = false;

static void publish() {
  if (!found) { etcore.sendError("sensor", "INA219 not found: check SDA/SCL"); return; }
  float v = ina.getBusVoltage_V() + ina.getShuntVoltage_mV() / 1000.0f;
  float a = ina.getCurrent_mA() / 1000.0f;
  JsonDocument d;
  d["voltage"] = roundf(v * 100) / 100;
  d["current"] = roundf(a * 1000) / 1000;
  d["power"] = roundf(v * a * 100) / 100;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(0x40);
  found = Wire.endTransmission() == 0;  // is the INA219 there?
  if (found) ina.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("power1", "sensor.power", "DC Power Monitor", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
