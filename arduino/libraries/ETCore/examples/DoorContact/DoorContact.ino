// ET-Core door / window contact (class binary_sensor.door): a reed switch and a
// magnet. Home Assistant shows "Open" / "Closed". For a window use class
// "binary_sensor.window", for a garage door "binary_sensor.garage_door".
// Wiring: reed switch between DOOR_PIN and GND (closed = magnet near = door shut).
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define DOOR_PIN  4

ETCore etcore;
bool open_ = false;

static void report() {
  JsonDocument d;
  d["on"] = open_;   // on = open
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(DOOR_PIN, INPUT_PULLUP);
  open_ = digitalRead(DOOR_PIN) == HIGH;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("door1", "binary_sensor.door", "Front Door", "1.0");
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
  static uint32_t changedAt = 0;
  bool now = digitalRead(DOOR_PIN) == HIGH;
  if (now != open_ && millis() - changedAt > 50) {  // debounce
    changedAt = millis();
    open_ = now;
    report();
    Serial.println(open_ ? "open" : "closed");
  }
}
