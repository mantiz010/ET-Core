// ET-Core motion sensor (class binary_sensor.motion): an HC-SR501 / AM312 PIR,
// or a microwave radar such as the RCWL-0516. Home Assistant shows
// "Detected" / "Clear" and can switch lights on when someone walks in.
// Motion is sent the moment it starts; "clear" after HOLD_MS with no motion.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define PIR_PIN   4
#define HOLD_MS   30000

ETCore etcore;
bool motion = false;
uint32_t lastSeen = 0;

static void report() {
  JsonDocument d;
  d["on"] = motion;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("motion1", "binary_sensor.motion", "Hallway Motion", "1.0");
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
  if (digitalRead(PIR_PIN) == HIGH) {
    lastSeen = millis();
    if (!motion) { motion = true; report(); Serial.println("motion"); }
  } else if (motion && millis() - lastSeen > HOLD_MS) {
    motion = false;
    report();
    Serial.println("clear");
  }
}
