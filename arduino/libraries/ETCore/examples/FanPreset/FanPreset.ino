// ET-Core 3-speed fan (class fan.preset): a ceiling or extractor fan with fixed
// speeds switched by relays. Home Assistant shows a fan with Low / Medium / High.
// Only one speed relay is ever on; there is a short gap when changing speed so
// two windings are never powered together.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

static const char* PRESETS[]    = {"low", "medium", "high"};
#if CONFIG_IDF_TARGET_ESP32C3
static const uint8_t RELAY_PINS[] = {4, 5, 6};     // one relay per speed
#else
static const uint8_t RELAY_PINS[] = {25, 26, 27};  // GPIO6-11 are the flash on a standard ESP32
#endif
static const uint8_t COUNT = 3;

ETCore etcore;
bool fanOn = false;
uint8_t speed = 0;  // index into PRESETS

static void applyRelays() {
  for (uint8_t i = 0; i < COUNT; i++) digitalWrite(RELAY_PINS[i], LOW);
  delay(100);  // break before make
  if (fanOn) digitalWrite(RELAY_PINS[speed], HIGH);
}

static void report() { etcore.sendFanPreset(fanOn, PRESETS[speed]); }

// Home Assistant's preset list comes from the hello.
static void onHello(JsonObject hello) {
  JsonArray a = hello["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < COUNT; i++) a.add(PRESETS[i]);
}

void onCommand(const char* cls, JsonObject p) {
  if (p["on"].is<bool>()) fanOn = p["on"];
  if (p["preset"].is<const char*>()) {
    for (uint8_t i = 0; i < COUNT; i++) {
      if (strcmp(p["preset"], PRESETS[i]) == 0) { speed = i; fanOn = true; }
    }
  }
  applyRelays();
  report();
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < COUNT; i++) { pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], LOW); }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.onHello(onHello);
  etcore.begin("fan3speed", "fan.preset", "Ceiling Fan", "1.0");
  etcore.onCommand(onCommand);
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
}
