// ET-Core PWM fan (class fan.speed: 0-100 %). For a fan with fixed steps use class
// "fan.preset", read payload["preset"] ("off", "low", "medium", "high") and report
// it with sendFanPreset(on, preset).
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same key as in Home Assistant

static const int FAN_PIN = 25;

ETCore etcore;
bool on = false;
uint8_t speedPct = 0;

static void applyFan() { analogWrite(FAN_PIN, on ? (uint16_t)speedPct * 255 / 100 : 0); }
static void publishState() { etcore.sendFanSpeed(on, speedPct); }

static void onCommand(const char* dev_class, JsonObject payload) {
  if (payload["on"].is<bool>()) on = payload["on"];
  if (payload["speed"].is<int>()) speedPct = constrain(payload["speed"].as<int>(), 0, 100);
  if (on && speedPct == 0) speedPct = 100;
  applyFan();
  publishState();
}

void setup() {
  pinMode(FAN_PIN, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  etcore.begin("fan1", "fan.speed", "Fan 1", "fan-1.0");
  etcore.onCommand(onCommand);
  etcore.onSync(publishState);
  applyFan();
  publishState();
}

void loop() {
  etcore.loop();
}
