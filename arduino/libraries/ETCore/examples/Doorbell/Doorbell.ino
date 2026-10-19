// ET-Core doorbell / push button (class binary_sensor.button).
// A press shows as "on" in Home Assistant for a moment, so an automation can
// trigger on it: ring a speaker, send a phone notification, flash the lights.
// Wiring: button between BUTTON_PIN and GND (the pin's pull-up is used).
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#if CONFIG_IDF_TARGET_ESP32C3
#define BUTTON_PIN  9   // BOOT button on most ESP32-C3 boards
#else
#define BUTTON_PIN  0   // BOOT button on ESP32 DevKit boards (GPIO6-11 are the flash)
#endif
#define HOLD_MS     1500  // how long Home Assistant shows "pressed"

ETCore etcore;
bool pressed = false;
uint32_t pressedAt = 0;

static void report() {
  JsonDocument d;
  d["on"] = pressed;
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.begin("doorbell1", "binary_sensor.button", "Doorbell", "1.0");
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
  static bool last = HIGH;
  bool now = digitalRead(BUTTON_PIN);
  if (now == LOW && last == HIGH && millis() - pressedAt > 300) {  // debounce
    pressed = true;
    pressedAt = millis();
    report();
    Serial.println("ding dong");
  }
  last = now;
  if (pressed && millis() - pressedAt > HOLD_MS) {
    pressed = false;
    report();
  }
}
