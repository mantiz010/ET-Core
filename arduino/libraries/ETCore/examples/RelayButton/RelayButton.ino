// ET-Core relay with a wall button (class switch.relay).
// - Home Assistant switches the relay; the button toggles it locally too, even
//   when WiFi or Home Assistant is down.
// - The state is saved, so after a power cut the relay comes back as it was.
// - A status LED shows the relay state.
// Wiring: relay module IN -> RELAY_PIN, push button between BUTTON_PIN and GND.
#include <WiFi.h>
#include <Preferences.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define RELAY_PIN     4
#if CONFIG_IDF_TARGET_ESP32C3
#define BUTTON_PIN    9        // BOOT button on most ESP32-C3 boards
#define LED_PIN       8        // status LED, -1 for none
#else
#define BUTTON_PIN    0        // BOOT button on ESP32 DevKit boards (GPIO6-11 are the flash)
#define LED_PIN       2        // on-board LED on most ESP32 DevKits, -1 for none
#endif
#define RELAY_ACTIVE  HIGH     // LOW for relay boards that switch on a low input

ETCore etcore;
Preferences prefs;
bool relayOn = false;

static void apply(bool on, bool report) {
  relayOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ACTIVE : !RELAY_ACTIVE);
  if (LED_PIN >= 0) digitalWrite(LED_PIN, on);
  prefs.putBool("on", on);
  if (report) etcore.sendSwitchState(on);
}

void onCommand(const char* cls, JsonObject p) {
  if (p["on"].is<bool>()) apply(p["on"], false);
  etcore.sendSwitchState(relayOn);
}

// Debounced button: one toggle per press.
static void serviceButton() {
  static bool last = HIGH;
  static uint32_t changedAt = 0;
  static bool handled = false;
  bool now = digitalRead(BUTTON_PIN);
  if (now != last) { last = now; changedAt = millis(); handled = false; }
  if (!handled && now == LOW && millis() - changedAt > 40) {
    handled = true;
    apply(!relayOn, true);
    Serial.printf("button -> %s\n", relayOn ? "on" : "off");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);
  prefs.begin("relaybtn", false);
  apply(prefs.getBool("on", false), false);  // before WiFi: the light works at once

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { serviceButton(); delay(10); }

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.begin("relaybtn1", "switch.relay", "Relay with Button", "1.0");
  etcore.onCommand(onCommand);
  etcore.onSync([]() { etcore.sendSwitchState(relayOn); });
  etcore.sendSwitchState(relayOn);
}

void loop() {
  etcore.loop();
  serviceButton();
}
