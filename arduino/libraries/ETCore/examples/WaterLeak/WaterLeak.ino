// ET-Core water leak detector (class binary_sensor.moisture): two bare wires or
// a leak probe on the floor under a washing machine, dishwasher or hot water
// cylinder. Home Assistant shows "Wet" / "Dry"; also sends a device error so
// the ET-Core page lists the leak as a problem. Optional buzzer sounds locally.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define PROBE_PIN   4      // probe between PROBE_PIN and GND
#define BUZZER_PIN  -1     // a buzzer pin, or -1

ETCore etcore;
bool wet = false;

static void report() {
  JsonDocument d;
  d["on"] = wet;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(PROBE_PIN, INPUT_PULLUP);
  if (BUZZER_PIN >= 0) pinMode(BUZZER_PIN, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("leak1", "binary_sensor.moisture", "Laundry Leak", "1.0");
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
  // Water closes the probe (LOW). Needs 1 s of steady wet/dry to change state.
  static uint32_t since = 0;
  bool now = digitalRead(PROBE_PIN) == LOW;
  if (now == wet) since = millis();
  else if (millis() - since > 1000) {
    wet = now;
    report();
    if (wet) etcore.sendError("leak", "Water detected");
    Serial.println(wet ? "WET" : "dry");
  }
  if (BUZZER_PIN >= 0) digitalWrite(BUZZER_PIN, wet && (millis() / 300) % 2);
}
