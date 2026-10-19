// ET-Core battery monitor (class sensor.battery): Voltage (V) and Battery (%)
// of a battery bank: 12 V lead-acid / LiFePO4 for solar, a caravan, a boat or
// a UPS. Reads it through a resistor divider into an ADC pin.
//
// Power the ESP32 from mains, or from that big bank through a 5 V buck
// converter. Do NOT run it from a small cell: an ET-Core device stays on WiFi
// all the time (it checks in every 2 s so Home Assistant knows it is alive),
// about 80-120 mA, so a 2000 mAh cell lasts roughly a day. ET-Core is for
// powered devices; battery sensors that sleep need Zigbee or ESP-NOW.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#if CONFIG_IDF_TARGET_ESP32C3
#define ADC_PIN        2   // ADC1 on the ESP32-C3
#else
#define ADC_PIN        35  // ADC1 (GPIO32-39); ADC2 pins stop working while Wi-Fi is on
#endif
#define DIVIDER        (100.0f + 22.0f) / 22.0f   // R1 = 100k (to battery), R2 = 22k (to GND)
#define EMPTY_V        11.8f                      // 0 %   (LiFePO4 12 V: 12.0)
#define FULL_V         12.7f                      // 100 % (LiFePO4 12 V: 13.4)
#define EVERY_MS       60000

ETCore etcore;

static void publish() {
  uint32_t mv = 0;
  for (int i = 0; i < 32; i++) mv += analogReadMilliVolts(ADC_PIN);
  float v = mv / 32.0f / 1000.0f * DIVIDER;
  float pct = constrain((v - EMPTY_V) / (FULL_V - EMPTY_V) * 100.0f, 0.0f, 100.0f);
  JsonDocument d;
  d["voltage"] = roundf(v * 100) / 100;
  d["battery"] = roundf(pct);
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("battery1", "sensor.battery", "Battery Bank", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
