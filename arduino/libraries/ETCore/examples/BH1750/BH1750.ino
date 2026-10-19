// ET-Core BH1750 light level (class sensor.lux): illuminance in lux. Use it to
// switch lights on at dusk or close blinds in strong sun.
// Sends at once when the light changes a lot, otherwise once a minute.
// Needs: "BH1750" by Christopher Laws. Wiring (I2C): SDA, SCL, 3.3V, GND.
#include <WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

// The board's own I2C pins (ESP32: 21/22, ESP32-C3: 8/9). GPIO6-11 are the flash
// on a standard ESP32, so never hard-code 8/9 there.
#define SDA_PIN  SDA
#define SCL_PIN  SCL

ETCore etcore;
BH1750 meter;
float sent = -1;

static void publish() {
  float lux = meter.readLightLevel();
  if (lux < 0) { etcore.sendError("sensor", "BH1750 not answering: check SDA/SCL"); return; }
  etcore.sendValue(roundf(lux), "lx");
  sent = lux;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("lux1", "sensor.lux", "Light Level", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t lastCheck = 0, lastSend = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  float lux = meter.readLightLevel();
  bool bigChange = sent < 0 || fabsf(lux - sent) > max(10.0f, sent * 0.2f);  // 20 %
  if (bigChange || millis() - lastSend > 60000) { lastSend = millis(); publish(); }
}
